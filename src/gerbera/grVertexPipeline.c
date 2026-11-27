/* grVertexPipeline.c - vertex pipeline (grVertexPipeline.inl).
 * Assert sites keep the original grVertexPipeline.inl file/line strings. */
#include "gerbera/grState.h"

#define GR_VP_CHECK(cond, line)                  \
    do                                           \
    {                                            \
        if (!(cond) && grAssert(#cond, "grVertexPipeline.inl", (line)) != 0) \
            grDie(0);                            \
    } while (0)

unsigned int grTestLightColor(_grLight *light, int component)
{
    GR_VP_CHECK(light != NULL, 393);
    if (XColor3_isZero(&light->colorsx[component]) != 0)
        return 0;
    return 0xFFFFFFFFu;
}

unsigned int grPrecalcLightColor(_grLight *light, int component, _XColor3 *materialColor)
{
    GR_VP_CHECK(light != NULL && materialColor != NULL, 384);
    XColor3_mul(&light->premul_colors[component], &light->colorsx[component], materialColor);
    if (XColor3_isZero(&light->premul_colors[component]) != 0)
        return 0;
    return 0xFFFFFFFFu;
}

/* --- grUpdateLight (decompile 28169-28201; fragment assert line 410) -------
 * Recomputes one light's lightMask and, for directional lights, the
 * headlight "halfway" vector. flt_48E050 = 180.0 (the default spot cutoff;
 * the EPOC build compares the raw dword against 0x43340000, which is
 * IScalar_immediate(180.0f) since immediates are float bit patterns). */

unsigned int grUpdateLight(Gerbera_s *state, int ndx)
{
    unsigned int lightMask = 0;
    _grLight *l;

    if (!(hgInRange32(ndx, 0, GR_MAX_LIGHTS - 1) != 0) && grAssert("hgInRange32(ndx, 0, GR_MAX_LIGHTS-1)", "grVertexPipeline.inl", 410) != 0)
        grDie(0);
    l = &state->lights[ndx];
    if (IScalar_cmpeq(l->spotCutoff, IScalar_immediate(180.0f)) == 0)
        lightMask = 16u;
    if (l->positional != 0)
    {
        if (IFVector3_is100(&l->attenuation) == 0)
            lightMask |= 0x20u;
    }
    else
    {
        lightMask |= 0x40u;
        l->halfway[0] = l->nVP[0];
        l->halfway[1] = l->nVP[1];
        l->halfway[2] = XScalar_add(l->nVP[2], 0x200000);
        XNormal3_normalize(l->halfway, l->halfway);
    }
    l->lightMask = lightMask;
    return lightMask;
}

/* --- grUpdateAmbientLightSum (decompile 28204-28220) ------------------------
 * ambientLightSum = ambientColor + ambient color of every directional
 * light (the pointer list built by grUpdateLights). */

void grUpdateAmbientLightSum(Gerbera_s *state)
{
    _grLight **pDirectional;

    state->ambientLightSum = state->ambientColor;
    for (pDirectional = state->l_directional; *pDirectional != NULL; ++pDirectional)
        XColor3_add(&state->ambientLightSum, &state->ambientLightSum, &(*pDirectional)->colorsx[0]);
}

/* --- grUpdateLights (decompile 28223-28551; fragment assert lines
 * 518/609/660/685/702) -------------------------------------------------------
 * Refreshes light records whose type/color changed, partitions the enabled
 * lights into l_directional / l_positional pointer lists, recomputes the
 * premultiplied light colors against the material and refreshes
 * ambientLightSum / baseColor. Returns whether a positional light is on
 * (the vertex pipeline's transformVE bit). */

int grUpdateLights(Gerbera_s *state)
{
    unsigned int activeMask;
    unsigned char typeDirty;
    unsigned int activeDirtyMask;
    unsigned int colorDirtyMask;
    unsigned int directionalMask;
    unsigned int dirty;
    unsigned int lightMask;
    int i;
    _grLight *pLight;
    _grLight **pDir;
    _grLight **pPos;
#if HG_ASSERT_ENABLED
    _XColor3 c;
#endif
    _XColor3 tmp;
    int recompute;

    activeMask = state->lightActiveMask;
    typeDirty = (unsigned char)(activeMask & state->lightTypeDirtyMask);
    activeDirtyMask = state->lightActiveDirtyMask;
    colorDirtyMask = state->lightColorDirtyMask;
    directionalMask = state->lightDirectionalMask;
    if (typeDirty != 0)
    {
        activeDirtyMask |= typeDirty;
        for (i = 0; i < 8; ++i)
        {
            if (((1 << i) & typeDirty) != 0)
            {
                if ((grUpdateLight(state, i) & 0x40) != 0)
                    directionalMask |= 1 << i;
                else
                    directionalMask &= ~(1u << i);
            }
        }
        state->lightTypeDirtyMask &= ~typeDirty;
        state->lightDirectionalMask = (unsigned char)directionalMask;
    }
    directionalMask &= activeMask;
    for (i = 0; i < 8; ++i)
    {
        if (grTestCapability(state, GR_CAPABILITY_LIGHT0 + i) != 0)
        {
            if (!((activeMask & (1 << i)) != 0) && grAssert("lightActiveMask & (1<<i)", "grVertexPipeline.inl", 518) != 0)
                grDie(0);
            if ((state->lights[i].lightMask & 0x40) != 0)
            {
                if (!((directionalMask & (1 << i)) != 0) && grAssert("(lightDirectionalMask & (1<<i)) != 0", "grVertexPipeline.inl", 518) != 0)
                    grDie(0);
            }
            else
            {
                if (!((directionalMask & (1 << i)) == 0) && grAssert("(lightDirectionalMask & (1<<i)) == 0", "grVertexPipeline.inl", 518) != 0)
                    grDie(0);
            }
        }
        else
        {
            if (!((activeMask & (1 << i)) == 0) && grAssert("(lightActiveMask & (1<<i)) == 0", "grVertexPipeline.inl", 518) != 0)
                grDie(0);
            if (!((directionalMask & (1 << i)) == 0) && grAssert("(lightDirectionalMask & (1<<i)) == 0", "grVertexPipeline.inl", 518) != 0)
                grDie(0);
        }
    }
    if (activeDirtyMask != 0)
    {
        pDir = state->l_directional;
        pPos = state->l_positional;
        for (i = 0; i < 8; ++i)
        {
            if (((1 << i) & activeMask) != 0)
            {
                pLight = &state->lights[i];
                if ((pLight->lightMask & 0x40) != 0)
                    *pDir++ = pLight;
                else
                    *pPos++ = pLight;
            }
        }
        *pDir = NULL;
        *pPos = NULL;
        state->lightActiveDirtyMask = 0;
    }
    dirty = activeMask & (colorDirtyMask | typeDirty);
    recompute = state->baseColorDirty;
    if (grTestCapability(state, GR_CAPABILITY_COLOR_MATERIAL) != 0)
    {
        if (dirty != 0)
        {
            for (i = 0; i < 8; ++i)
            {
                if (((1 << i) & dirty) != 0)
                {
                    pLight = &state->lights[i];
                    lightMask = pLight->lightMask & 0xFFFFFFF1u;
                    if ((lightMask & 0x40) == 0)
                        lightMask |= grTestLightColor(pLight, 0) & 2u;
                    lightMask |= grTestLightColor(pLight, 1) & 4u;
                    lightMask |= grPrecalcLightColor(pLight, 2, &state->material.colors[2]) & 8u;
                    pLight->lightMask = lightMask;
                }
            }
            state->lightColorDirtyMask &= ~(unsigned int)(unsigned char)activeMask;
        }
#if HG_ASSERT_ENABLED
        for (i = 0; i < 8; ++i)
        {
            if (grGetCapability(state, GR_CAPABILITY_LIGHT0 + i) != 0)
            {
                pLight = &state->lights[i];
                lightMask = pLight->lightMask & 0xFFFFFFF1u;
                if ((lightMask & 0x40) == 0)
                    lightMask |= grTestLightColor(pLight, 0) & 2u;
                lightMask |= grTestLightColor(pLight, 1) & 4u;
                lightMask |= grPrecalcLightColor(pLight, 2, &state->material.colors[2]) & 8u;
                if (!((unsigned int)pLight->lightMask == lightMask) && grAssert("pLight->lightMask == lightMask", "grVertexPipeline.inl", 660) != 0)
                    grDie(0);
            }
        }
#endif
    }
    else
    {
        if (dirty != 0)
        {
            for (i = 0; i < 8; ++i)
            {
                if (((1 << i) & dirty) != 0)
                {
                    pLight = &state->lights[i];
                    lightMask = pLight->lightMask & 0xFFFFFFF1u;
                    if ((lightMask & 0x40) == 0)
                        lightMask |= grPrecalcLightColor(pLight, 0, &state->material.colors[0]) & 2u;
                    lightMask |= grPrecalcLightColor(pLight, 1, &state->material.colors[1]) & 4u;
                    lightMask |= grPrecalcLightColor(pLight, 2, &state->material.colors[2]) & 8u;
                    pLight->lightMask = lightMask;
                }
            }
            state->lightColorDirtyMask &= ~(unsigned int)(unsigned char)activeMask;
        }
#if HG_ASSERT_ENABLED
        for (i = 0; i < 8; ++i)
        {
            if (grGetCapability(state, GR_CAPABILITY_LIGHT0 + i) != 0)
            {
                pLight = &state->lights[i];
                lightMask = pLight->lightMask & 0xFFFFFFF1u;
                if ((lightMask & 0x40) == 0)
                    lightMask |= grPrecalcLightColor(pLight, 0, &state->material.colors[0]) & 2u;
                lightMask |= grPrecalcLightColor(pLight, 1, &state->material.colors[1]) & 4u;
                lightMask |= grPrecalcLightColor(pLight, 2, &state->material.colors[2]) & 8u;
                if (!((unsigned int)pLight->lightMask == lightMask) && grAssert("pLight->lightMask == lightMask", "grVertexPipeline.inl", 609) != 0)
                    grDie(0);
            }
        }
#endif
    }
    if (state->ambientLightSumDirty != 0 || (directionalMask & colorDirtyMask) != 0 || directionalMask != state->ambientLightSumMask)
    {
        grUpdateAmbientLightSum(state);
        recompute = 1;
        state->ambientLightSumMask = (unsigned char)directionalMask;
        state->ambientLightSumDirty = 0;
    }
#if HG_ASSERT_ENABLED
    c = state->ambientLightSum;
    grUpdateAmbientLightSum(state);
    if (!(c.r == state->ambientLightSum.r) && grAssert("c->r == state->ambientLightSum->r", "grVertexPipeline.inl", 685) != 0)
        grDie(0);
    if (!(c.g == state->ambientLightSum.g) && grAssert("c->g == state->ambientLightSum->g", "grVertexPipeline.inl", 685) != 0)
        grDie(0);
    if (!(c.b == state->ambientLightSum.b) && grAssert("c->b == state->ambientLightSum->b", "grVertexPipeline.inl", 685) != 0)
        grDie(0);
#endif
    if (recompute != 0)
    {
        XColor3_mul(&tmp, &state->ambientLightSum, &state->material.colors[0]);
        XColor3_add(&state->baseColor, &tmp, &state->material.colors[3]);
        state->baseColorDirty = 0;
    }
#if HG_ASSERT_ENABLED
    XColor3_mul(&tmp, &state->ambientLightSum, &state->material.colors[0]);
    XColor3_add(&tmp, &tmp, &state->material.colors[3]);
    if (!(tmp.r == state->baseColor.r) && grAssert("c->r == state->baseColor->r", "grVertexPipeline.inl", 702) != 0)
        grDie(0);
    if (!(tmp.g == state->baseColor.g) && grAssert("c->g == state->baseColor->g", "grVertexPipeline.inl", 702) != 0)
        grDie(0);
    if (!(tmp.b == state->baseColor.b) && grAssert("c->b == state->baseColor->b", "grVertexPipeline.inl", 702) != 0)
        grDie(0);
#endif
    return state->l_positional[0] != NULL;
}

/* --- vertex fetch buffer ----------------------------------------------------
 * The fetch-stage vertex records live in the grFast render stack: base =
 * renderStack[166].whichPool = (char*)renderStack + 1332, one 128-byte
 * _grVertex per vertex index (<< 7 addressing). */

#define GR_RENDER_STACK_BYTE(state, offset) \
    ((unsigned char *)(state)->renderStack + (offset))
#undef GR_VERTEX_BUFFER
#define GR_VERTEX_BUFFER(state) \
    ((_grVertex *)GR_RENDER_STACK_BYTE(state, 1332))

#define GR_FETCH_CHECK(vertexList, line)                                 \
    do                                                                   \
    {                                                                    \
        int ok__ = 1;                                                    \
        int cond__ = ((vertexList) != NULL && *(vertexList) != 0xFF);    \
        if (!cond__ && grAssert("vertexList && (* vertexList!=(HGuint8)GR_INVALID_INDEX)", \
                                "grVertexPipeline.inl", (line)) != 0)    \
            ok__ = 0;                                                \
        if (ok__ == 0)                                                   \
            grDie(0);                                                    \
    } while (0)

/* --- grGetVertexPositionState / grSetVertexPositionState (27797/27813) ------ */

int grGetVertexPositionState(_grVertex *v)
{
    return (int)v->stateMask >> 14;
}

/* --- grGetFogZ (30145-30180; assert grVertexPipeline.inl 1911) --------------
 * The eye-space Z of a vertex depending on how far grTransformVertex got. */

static inline int grVp_zmulfv4Raw3(const _IVertexTransform *pT, const int *src)
{
    int v0 = src[0], v1 = src[1], v2 = src[2];
    unsigned int e0 = (unsigned int)(unsigned char)((unsigned int)v0 >> 23);
    unsigned int e1 = (unsigned int)(unsigned char)((unsigned int)v1 >> 23);
    unsigned int e2 = (unsigned int)(unsigned char)((unsigned int)v2 >> 23);

    if ((pT->cl & 0xFF0000u) == 0x200000u)
        return IScalar_iftos(v2);

    if (e0 != 255u && e1 != 255u && e2 != 255u)
    {
        const _IFVector4 *pA = &pT->v[2];
        int s0 = v0 >> 31, s1 = v1 >> 31, s2 = v2 >> 31;
        int m0 = e0 ? (int)(((((unsigned int)v0 & 0x7FFFFFu) | 0x800000u) << 6 ^ (unsigned int)s0) - (unsigned int)s0) : 0;
        int m1 = e1 ? (int)(((((unsigned int)v1 & 0x7FFFFFu) | 0x800000u) << 6 ^ (unsigned int)s1) - (unsigned int)s1) : 0;
        int m2 = e2 ? (int)(((((unsigned int)v2 & 0x7FFFFFu) | 0x800000u) << 6 ^ (unsigned int)s2) - (unsigned int)s2) : 0;
        unsigned int pae = pA->e;
        int ea0 = (int)(pae & 0xFFu);
        int ea1 = (int)((pae >> 8) & 0xFFu);
        int ea2 = (int)((pae >> 16) & 0xFFu);
        int ea3 = (int)((pae >> 24) & 0xFFu);
        int ee0 = (ea0 != 0 && e0 != 0u) ? ea0 + (int)e0 : 0;
        int ee1 = (ea1 != 0 && e1 != 0u) ? ea1 + (int)e1 : 0;
        int ee2 = (ea2 != 0 && e2 != 0u) ? ea2 + (int)e2 : 0;
        int ee3 = (ea3 != 0) ? ea3 + 127 : 0;
        int maxE = ee0;
        int sh0, sh1, sh2, sh3, m, z;

        if (maxE < ee1) maxE = ee1;
        if (maxE < ee2) maxE = ee2;
        if (maxE < ee3) maxE = ee3;
        sh0 = maxE - ee0; if (sh0 > 31) sh0 = 31;
        sh1 = maxE - ee1; if (sh1 > 31) sh1 = 31;
        sh2 = maxE - ee2; if (sh2 > 31) sh2 = 31;
        sh3 = maxE - ee3; if (sh3 > 31) sh3 = 31;
        m = hgMul64h(pA->m[0], m0) >> sh0;
        m += hgMul64h(pA->m[1], m1) >> sh1;
        m += hgMul64h(pA->m[2], m2) >> sh2;
        m += (pA->m[3] >> 3) >> sh3;
        if (m == 0)
            return IScalar_zero();
        z = IScalar_setAndNormalize((unsigned int)(maxE - 125), m);
        if (IScalar_isValid(z) == 0)
            imAssert("IScalar_isValid(z)", "IMatrix4.c", 1789);
        return z;
    }
    {
        _IFVector4 tmp;
        IFVector4_setfv(&tmp, (int *)src, 3);
        return IVertexTransform_zmulfv4((_IVertexTransform *)pT, &tmp);
    }
}

int grGetFogZ(Gerbera_s *state, _grVertex *vertex)
{
    int result;

    switch (grGetVertexPositionState(vertex))
    {
        case 0:
            if ((vertex->stateMask & GR_VERTEXMASK_APPROXIMATE_POSITION) != 0 &&
                state->vertexArray[0].data != NULL)
            {
                const _grVertexArray *array = &state->vertexArray[0];
                const int *src = (const int *)((const unsigned char *)array->data + array->stride * vertex->index);
                if (array->type == GR_DATATYPE_FLOAT && array->size == 3)
                    result = grVp_zmulfv4Raw3(&state->modelviewTransform, src);
                else
                {
                    _IFVector4 tmp;
                    IFVector4_setfv(&tmp, (int *)src, array->size);
                    result = IVertexTransform_zmulfv4(&state->modelviewTransform, &tmp);
                }
            }
            else
            {
                result = IVertexTransform_zmulfv4(&state->modelviewTransform,
                                                  vertex->position.objectSpaceFloat);
            }
            break;
        case 1:
            result = IVertexTransform_zmulxv4(&state->modelviewTransform,
                                              (int *)&vertex->position);
            break;
        case 2:
            result = IFVector4_getf(vertex->position.objectSpaceFloat, 2);
            break;
        default:
            if (grGetVertexPositionState(vertex) != 3 &&
                grAssert("grGetVertexPositionState(vertex) == GR_POSITION_EYE_SPACE_CARTESIAN",
                         "grVertexPipeline.inl", 1911) != 0)
                grDie(0);
            result = vertex->position.objectSpaceFloat[0].m[3];
            break;
    }
    return result;
}

/* --- fog (fragments 2254-2416) ----------------------------------------------
 * All four write the fog blend factor into color.final[1][3] (u16). */

void grApplyFogFAST(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1886);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->color.final[1][3] = 0xFFFFu;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

#if defined(__GNUC__) || defined(__clang__)
static unsigned short grApplyFogLINEAR_zeroZ(unsigned int fogEndBits, unsigned int fogOOBits)
    __asm__("grApplyFogLINEAR_zeroZ_asm") __attribute__((noinline, used));
static unsigned short grApplyFogLINEAR_slow(Gerbera_s *state, _grVertex *vertex)
    __asm__("grApplyFogLINEAR_slow_asm") __attribute__((noinline, used));
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static unsigned short grApplyFogLINEAR_zeroZ(unsigned int fogEndBits, unsigned int fogOOBits)
{
    return (unsigned short)~(unsigned short)hgClamp32_0_x(
        IScalar_scalarToFixed((HGint32)F_MUL(fogEndBits, fogOOBits), 16), 0xFFFF);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static unsigned short grApplyFogLINEAR_slow(Gerbera_s *state, _grVertex *vertex)
{
    int d = IScalar_abs(grGetFogZ(state, vertex));
    int fogOORange = state->fogOORange;
    return (unsigned short)~(unsigned short)hgClamp32_0_x(
        IScalar_scalarToFixed(IScalar_mul(IScalar_sub(state->fogEnd, d), fogOORange), 16),
        0xFFFF);
}

#if defined(__GNUC__) && defined(__arm__) && !defined(__thumb__) && \
    (defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_5TE__) || \
     defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || \
     (defined(__ARM_ARCH) && __ARM_ARCH >= 5)) && \
    !HG_ASSERT_ENABLED
typedef char gr_check_arm_fog_offsets_[
    (offsetof(Gerbera_s, modelviewTransform.v[2]) == 1256 &&
     offsetof(Gerbera_s, renderStack) == 5240 &&
     offsetof(Gerbera_s, vertexArray[0].stride) == 5404 &&
     offsetof(Gerbera_s, vertexArray[0].data) == 5412 &&
     offsetof(Gerbera_s, fogEnd) == 5952 &&
     offsetof(Gerbera_s, fogOORange) == 5956 &&
     offsetof(_grVertex, index) == 0 &&
     offsetof(_grVertex, stateMask) == 4 &&
     offsetof(_grVertex, color.final[1][3]) == 54 &&
     sizeof(_grVertex) == 128 &&
     GR_POSITION_OBJECT_SPACE_FLOAT == 0) ? 1 : -1];

__attribute__((naked, noinline))
static void grApplyFogLINEAR_float3Fast(Gerbera_s *state, unsigned char *vertexList)
{
    (void)state;
    (void)vertexList;
    __asm__ __volatile__(
        "stmfd sp!, {r4-r11, lr}\n\t"
        "sub   sp, sp, #44\n\t"
        "str   r0, [sp, #40]\n\t"       /* [sp+40] = state (for rare slow path) */
        "mov   r4, r1\n\t"              /* r4 = vertexList */
        "add   ip, r0, #4096\n\t"
        "ldr   r5, [ip, #1144]\n\t"     /* renderStack */
        "ldr   r6, [ip, #1308]\n\t"     /* r6 = stride */
        "ldr   r7, [ip, #1316]\n\t"     /* r7 = data */
        "add   r1, r0, #1024\n\t"
        "add   r1, r1, #232\n\t"
        "ldmia r1, {r2, r3, r8, r9, r10}\n\t" /* exps, m0, m1, m2, m3 */
        "ldr   r0, [ip, #1856]\n\t"     /* fogEnd */
        "ldr   r1, [ip, #1860]\n\t"     /* fogOORange */
        "mov   r10, r10, asr #3\n\t"    /* m3_shr3 */
        "add   r5, r5, #1328\n\t"
        "add   r5, r5, #4\n\t"          /* r5 = vertexBuffer */
        "str   r3, [sp, #0]\n\t"        /* [sp+0] = m0 */
        "str   r8, [sp, #4]\n\t"        /* [sp+4] = m1 */
        "str   r9, [sp, #8]\n\t"        /* [sp+8] = m2 */
        "str   r2, [sp, #12]\n\t"       /* [sp+12] = exps */
        "str   r10, [sp, #16]\n\t"      /* [sp+16] = m3_shr3 */
        "mov   r9, r0, lsl #9\n\t"
        "mov   r9, r9, lsr #2\n\t"
        "orr   r9, r9, #0x40000000\n\t" /* fogEndMant */
        "mov   r10, r1, lsl #8\n\t"
        "orr   r10, r10, #0x80000000\n\t" /* fogOORangeMant */
        "mov   r8, r0, lsr #23\n\t"     /* fogEndExp */
        "rsb   r11, r8, #268\n\t"
        "sub   r11, r11, r1, lsr #23\n\t" /* kFog = 268 - fogEndExp - fogOORangeExp */
        "add   ip, sp, #20\n\t"
        "stmia ip, {r0, r9, r10, r11}\n\t"
        "str   r1, [sp, #36]\n\t"       /* fogOORange */
        "ldrb  r0, [r4], #1\n\t"
        "add   r9, r5, r0, lsl #7\n\t"

    "1:\n\t"
        "ldrh  r0, [r9, #4]\n\t"
        "movs  r0, r0, lsr #14\n\t"
        "bne   8f\n\t"
        "ldr   r0, [r9, #0]\n\t"
        "mla   r3, r6, r0, r7\n\t"
        "ldmia r3, {r0, r1, r2}\n\t"    /* r0=x, r1=y, r2=z (kept live!) */
        "mov   ip, #255\n\t"
        "and   r10, ip, r0, lsr #23\n\t"
        "and   r11, ip, r1, lsr #23\n\t"
        "and   lr,  ip, r2, lsr #23\n\t"
        "sub   r3, r10, #1\n\t"
        "cmp   r3, #254\n\t"
        "sublo r3, r11, #1\n\t"
        "cmplo r3, #254\n\t"
        "sublo r3, lr, #1\n\t"
        "cmplo r3, #254\n\t"
        "bhs   12f\n\t"

        "ldr   r8, [sp, #12]\n\t"       /* exps (ea0, ea1, ea2 non-zero) */
        "and   r3, r8, #255\n\t"
        "add   r10, r10, r3\n\t"
#if defined(__ARM_ARCH) && __ARM_ARCH >= 6
        "uxtb  r3, r8, ror #8\n\t"
        "add   r11, r11, r3\n\t"
        "uxtb  r3, r8, ror #16\n\t"
        "add   lr,  lr,  r3\n\t"
#else
        "and   r3, r8, #0xFF00\n\t"
        "add   r11, r11, r3, lsr #8\n\t"
        "mov   r3, r8, lsr #16\n\t"
        "and   r3, r3, #255\n\t"
        "add   lr,  lr,  r3\n\t"
#endif
        "movs  r8, r8, lsr #24\n\t"
        "addne r8, r8, #127\n\t"        /* ea3 */
        "mov   ip, r8\n\t"
        "cmp   ip, r10\n\t"
        "movlt ip, r10\n\t"
        "cmp   ip, r11\n\t"
        "movlt ip, r11\n\t"
        "cmp   ip, lr\n\t"
        "movlt ip, lr\n\t"              /* ip = maxExp */
        "orr   r11, r11, lr, lsl #16\n\t" /* r11 = eY | (eZ << 16); lr free */

        "ldr   r3, [sp, #16]\n\t"       /* m3_shr3 */
        "sub   r8, ip, r8\n\t"
        "cmp   r8, #31\n\t"
        "movgt r8, #31\n\t"
        "mov   r3, r3, asr r8\n\t"      /* r3 = sum */

        "sub   r10, ip, r10\n\t"
        "cmp   r10, #31\n\t"
        "movgt r10, #31\n\t"
        "ldr   r8, [sp, #0]\n\t"        /* m0 */
        "cmp   r0, #0\n\t"
        "orr   r0, r0, #0x00800000\n\t"
        "mov   r0, r0, lsl #8\n\t"
        "mov   r0, r0, lsr #2\n\t"
        "rsbmi r0, r0, #0\n\t"
        "smull lr, r0, r8, r0\n\t"
        "add   r3, r3, r0, asr r10\n\t"

        "mov   r0, r11, lsl #16\n\t"
        "sub   r0, ip, r0, lsr #16\n\t"
        "cmp   r0, #31\n\t"
        "movgt r0, #31\n\t"
        "ldr   r8, [sp, #4]\n\t"        /* m1 */
        "cmp   r1, #0\n\t"
        "orr   r1, r1, #0x00800000\n\t"
        "mov   r1, r1, lsl #8\n\t"
        "mov   r1, r1, lsr #2\n\t"
        "rsbmi r1, r1, #0\n\t"
        "smull lr, r1, r8, r1\n\t"
        "add   r3, r3, r1, asr r0\n\t"

        "mov   lr, r11, lsr #16\n\t"
        "sub   lr, ip, lr\n\t"
        "cmp   lr, #31\n\t"
        "movgt lr, #31\n\t"
        "ldr   r8, [sp, #8]\n\t"        /* m2 */
        "cmp   r2, #0\n\t"
        "orr   r2, r2, #0x00800000\n\t"
        "mov   r2, r2, lsl #8\n\t"
        "mov   r2, r2, lsr #2\n\t"
        "rsbmi r2, r2, #0\n\t"
        "smull r10, r2, r8, r2\n\t"
        "add   r3, r3, r2, asr lr\n\t"
    "4:\n\t"
        "cmp   r3, #0\n\t"
        "beq   7f\n\t"
        "rsblt r3, r3, #0\n\t"
        "clz   r1, r3\n\t"
        "sub   r1, r1, #1\n\t"
        "sub   ip, ip, r1\n\t"
        "sub   ip, ip, #124\n\t"
        "cmp   ip, #0\n\t"
        "ble   7f\n\t"
        "cmp   ip, #254\n\t"
        "bgt   6f\n\t"
        "cmp   r1, #0\n\t"
        "movge r3, r3, lsl r1\n\t"
        "movlt r3, r3, lsr #1\n\t"
        "mov   r0, r3, lsl #2\n\t"
        "mov   r0, r0, lsr #9\n\t"
        "orr   r0, r0, ip, lsl #23\n\t"
        "ldr   r10, [sp, #20]\n\t"      /* fogEnd */
        "cmp   r0, r10\n\t"
        "bhs   6f\n\t"

        "mov   r0, r10, lsr #23\n\t"    /* fogEndExp */
        "sub   r1, r0, ip\n\t"
        "cmp   r1, #31\n\t"
        "movhi r1, #31\n\t"
        "bic   r3, r3, #0x7F\n\t"
        "mov   r11, r3, lsr r1\n\t"
        "cmp   r3, r11, lsl r1\n\t"
        "addne r11, r11, #1\n\t"
        "add   r2, sp, #24\n\t"
        "ldmia r2, {r8, r10, lr}\n\t"   /* fogEndMant, fogOORangeMant, kFog */
        "sub   r3, r8, r11\n\t"
        "clz   ip, r3\n\t"
        "sub   ip, ip, #1\n\t"
        "subs  r0, r0, ip\n\t"
        "ble   6f\n\t"
        "mov   r3, r3, lsl ip\n\t"
        "mov   r3, r3, lsl #1\n\t"
        "bic   r3, r3, #0xFF\n\t"
        "umull r1, r11, r3, r10\n\t"
        "add   r0, lr, ip\n\t"
        "cmp   r11, #0\n\t"
        "sublt r0, r0, #1\n\t"
        "movlt r11, r11, lsr #1\n\t"
        "bic   r11, r11, #0x7F\n\t"
        "cmp   r0, #31\n\t"
        "bhi   5f\n\t"
        "mov   r11, r11, lsr r0\n\t"
        "movs  r0, r11, lsr #16\n\t"
        "mvn   r11, r11\n\t"
        "movne r11, #0\n\t"
        "strh  r11, [r9, #54]\n\t"
    "9:\n\t"
        "ldrb  r0, [r4], #1\n\t"
        "cmp   r0, #255\n\t"
        "addne r9, r5, r0, lsl #7\n\t"
        "bne   1b\n\t"
        "add   sp, sp, #44\n\t"
        "ldmfd sp!, {r4-r11, pc}\n\t"

    "5:\n\t"
        "cmp   r0, #0\n\t"
        "movlt r0, #0\n\t"
        "mvnge r0, #0\n\t"
        "strh  r0, [r9, #54]\n\t"
        "b     9b\n\t"
    "6:\n\t"
        "mvn   r0, #0\n\t"
        "strh  r0, [r9, #54]\n\t"
        "b     9b\n\t"
    "7:\n\t"
        "ldr   r0, [sp, #20]\n\t"
        "ldr   r1, [sp, #36]\n\t"
        "bl    grApplyFogLINEAR_zeroZ_asm\n\t"
        "strh  r0, [r9, #54]\n\t"
        "b     9b\n\t"
    "8:\n\t"
        "ldr   r0, [sp, #40]\n\t"
        "mov   r1, r9\n\t"
        "bl    grApplyFogLINEAR_slow_asm\n\t"
        "strh  r0, [r9, #54]\n\t"
        "b     9b\n\t"

    "12:\n\t"
        "cmp   r10, #255\n\t"
        "cmpne r11, #255\n\t"
        "cmpne lr,  #255\n\t"
        "beq   8b\n\t"
        "ldr   r8, [sp, #12]\n\t"
        "and   r3, r8, #255\n\t"
        "cmp   r10, #0\n\t"
        "cmpne r3, #0\n\t"
        "addne r10, r10, r3\n\t"
        "moveq r10, #0\n\t"
        "and   r3, r8, #0xFF00\n\t"
        "movs  r3, r3, lsr #8\n\t"
        "cmpne r11, #0\n\t"
        "addne r11, r11, r3\n\t"
        "moveq r11, #0\n\t"
        "mov   r3, r8, lsr #16\n\t"
        "ands  r3, r3, #255\n\t"
        "cmpne lr, #0\n\t"
        "addne lr, lr, r3\n\t"
        "moveq lr, #0\n\t"
        "movs  r8, r8, lsr #24\n\t"
        "addne r8, r8, #127\n\t"
        "mov   ip, r8\n\t"
        "cmp   ip, r10\n\t"
        "movlt ip, r10\n\t"
        "cmp   ip, r11\n\t"
        "movlt ip, r11\n\t"
        "cmp   ip, lr\n\t"
        "movlt ip, lr\n\t"
        "orr   r11, r11, lr, lsl #16\n\t"
        "ldr   r3, [sp, #16]\n\t"
        "sub   r8, ip, r8\n\t"
        "cmp   r8, #31\n\t"
        "movgt r8, #31\n\t"
        "mov   r3, r3, asr r8\n\t"
        "cmp   r10, #0\n\t"
        "beq   2f\n\t"
        "sub   r10, ip, r10\n\t"
        "cmp   r10, #31\n\t"
        "movgt r10, #31\n\t"
        "ldr   r8, [sp, #0]\n\t"
        "cmp   r0, #0\n\t"
        "orr   r0, r0, #0x00800000\n\t"
        "mov   r0, r0, lsl #8\n\t"
        "mov   r0, r0, lsr #2\n\t"
        "rsbmi r0, r0, #0\n\t"
        "smull lr, r0, r8, r0\n\t"
        "add   r3, r3, r0, asr r10\n\t"
    "2:\n\t"
        "movs  r0, r11, lsl #16\n\t"
        "beq   3f\n\t"
        "sub   r0, ip, r0, lsr #16\n\t"
        "cmp   r0, #31\n\t"
        "movgt r0, #31\n\t"
        "ldr   r8, [sp, #4]\n\t"
        "cmp   r1, #0\n\t"
        "orr   r1, r1, #0x00800000\n\t"
        "mov   r1, r1, lsl #8\n\t"
        "mov   r1, r1, lsr #2\n\t"
        "rsbmi r1, r1, #0\n\t"
        "smull lr, r1, r8, r1\n\t"
        "add   r3, r3, r1, asr r0\n\t"
    "3:\n\t"
        "movs  lr, r11, lsr #16\n\t"
        "beq   4b\n\t"
        "sub   lr, ip, lr\n\t"
        "cmp   lr, #31\n\t"
        "movgt lr, #31\n\t"
        "ldr   r8, [sp, #8]\n\t"
        "cmp   r2, #0\n\t"
        "orr   r2, r2, #0x00800000\n\t"
        "mov   r2, r2, lsl #8\n\t"
        "mov   r2, r2, lsr #2\n\t"
        "rsbmi r2, r2, #0\n\t"
        "smull r10, r2, r8, r2\n\t"
        "add   r3, r3, r2, asr lr\n\t"
        "b     4b\n\t"
    );
}
#endif

void grApplyFogLINEAR(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1925);

    if (state->fogEnd >= 0 && state->fogOORange > 0 &&
        state->vertexArray[0].data != NULL &&
        state->vertexArray[0].type == GR_DATATYPE_FLOAT &&
        state->vertexArray[0].size == 3 &&
        (state->modelviewTransform.cl & 0xFF0000u) != 0x200000u)
    {
        unsigned int fogEndBits = (unsigned int)state->fogEnd;
        unsigned int fogOOBits = (unsigned int)state->fogOORange;
        unsigned int eEnd = fogEndBits >> 23;
        unsigned int eOOR = fogOOBits >> 23;

        if (eEnd < 255u && eOOR >= 1u && eOOR < 255u)
        {
#if defined(__GNUC__) && defined(__arm__) && !defined(__thumb__) && \
    (defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_5TE__) || \
     defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || \
     (defined(__ARM_ARCH) && __ARM_ARCH >= 5)) && \
    !HG_ASSERT_ENABLED
            unsigned int paeFast = state->modelviewTransform.v[2].e;
            if ((paeFast & 0xFFu) != 0u &&
                (paeFast & 0xFF00u) != 0u &&
                (paeFast & 0xFF0000u) != 0u)
            {
                grApplyFogLINEAR_float3Fast(state, vertexList);
                return;
            }
#endif
            {
            const _grVertexArray *array = &state->vertexArray[0];
            const unsigned char *vbase = (const unsigned char *)array->data;
            int vstride = array->stride;
            const _IFVector4 *pA = &state->modelviewTransform.v[2];
            unsigned int pae = pA->e;
            int ea0 = (int)(pae & 0xFFu);
            int ea1 = (int)((pae >> 8) & 0xFFu);
            int ea2 = (int)((pae >> 16) & 0xFFu);
            int ea3 = (int)(pae >> 24);
            int ee3 = (ea3 != 0) ? ea3 + 127 : 0;
            int pm0 = pA->m[0];
            int pm1 = pA->m[1];
            int pm2 = pA->m[2];
            int pm3 = pA->m[3] >> 3;
            unsigned int sbOOR = (fogOOBits << 8) | 0x80000000u;
            unsigned int endMan = ((fogEndBits & 0x7FFFFFu) | 0x800000u) << 7;

            vertexBuffer = GR_VERTEX_BUFFER(state);
            vertexLista = vertexList + 1;
            vertex = &vertexBuffer[*vertexList];

            do
            {
                if (grGetVertexPositionState(vertex) == GR_POSITION_OBJECT_SPACE_FLOAT)
                {
                    const int *src = (const int *)(vbase + vstride * vertex->index);
                    int v0 = src[0], v1 = src[1], v2 = src[2];
                    unsigned int e0 = (unsigned int)(unsigned char)((unsigned int)v0 >> 23);
                    unsigned int e1 = (unsigned int)(unsigned char)((unsigned int)v1 >> 23);
                    unsigned int e2 = (unsigned int)(unsigned char)((unsigned int)v2 >> 23);

                    if (e0 != 255u && e1 != 255u && e2 != 255u)
                    {
                        int ee0 = (ea0 != 0 && e0 != 0u) ? ea0 + (int)e0 : 0;
                        int ee1 = (ea1 != 0 && e1 != 0u) ? ea1 + (int)e1 : 0;
                        int ee2 = (ea2 != 0 && e2 != 0u) ? ea2 + (int)e2 : 0;
                        int maxE = ee0;
                        int sh, m, ea;
                        unsigned int absM, mb, d;

                        if (maxE < ee1) maxE = ee1;
                        if (maxE < ee2) maxE = ee2;
                        if (maxE < ee3) maxE = ee3;
                        sh = maxE - ee3; if (sh > 31) sh = 31;
                        m = pm3 >> sh;
                        if (ee0 != 0)
                        {
                            unsigned int s0 = (unsigned int)(v0 >> 31);
                            int m0 = (int)((((((unsigned int)v0 & 0x7FFFFFu) | 0x800000u) << 6) ^ s0) - s0);
                            sh = maxE - ee0; if (sh > 31) sh = 31;
                            m += hgMul64h(pm0, m0) >> sh;
                        }
                        if (ee1 != 0)
                        {
                            unsigned int s1 = (unsigned int)(v1 >> 31);
                            int m1 = (int)((((((unsigned int)v1 & 0x7FFFFFu) | 0x800000u) << 6) ^ s1) - s1);
                            sh = maxE - ee1; if (sh > 31) sh = 31;
                            m += hgMul64h(pm1, m1) >> sh;
                        }
                        if (ee2 != 0)
                        {
                            unsigned int s2 = (unsigned int)(v2 >> 31);
                            int m2 = (int)((((((unsigned int)v2 & 0x7FFFFFu) | 0x800000u) << 6) ^ s2) - s2);
                            sh = maxE - ee2; if (sh > 31) sh = 31;
                            m += hgMul64h(pm2, m2) >> sh;
                        }
                        if (m == 0)
                        {
                            vertex->color.final[1][3] = grApplyFogLINEAR_zeroZ(fogEndBits, fogOOBits);
                            goto next_vtx;
                        }
                        absM = (unsigned int)(m < 0 ? -m : m);
                        sh = hgClz32(absM) - 1;
                        ea = (maxE - 125) - sh + 1;
                        if (ea <= 0)
                        {
                            vertex->color.final[1][3] = grApplyFogLINEAR_zeroZ(fogEndBits, fogOOBits);
                            goto next_vtx;
                        }
                        if ((unsigned int)ea > 254u)
                        {
                            vertex->color.final[1][3] = 0xFFFFu;
                            goto next_vtx;
                        }
                        mb = (sh >= 0) ? ((absM << sh) >> 1) : (absM >> 2);
                        d = ((unsigned int)ea << 23) | ((mb >> 6) & 0x7FFFFFu);
                        if (d >= fogEndBits)
                        {
                            vertex->color.final[1][3] = 0xFFFFu;
                        }
                        else
                        {
                            unsigned int expDiff = eEnd - (unsigned int)ea;
                            unsigned int bMan = (mb << 1) & 0xFFFFFF80u;
                            unsigned int bManFull = bMan;
                            unsigned int aMan, sa, prodHigh;
                            int subSh, aExp, expProd, fsh;

                            if (expDiff > 31u)
                                expDiff = 31u;
                            bMan >>= expDiff;
                            if (bManFull != (bMan << expDiff))
                                ++bMan;
                            aMan = endMan - bMan;
                            subSh = hgClz32(aMan) - 1;
                            aExp = (int)eEnd - subSh;
                            if (aExp <= 0)
                            {
                                vertex->color.final[1][3] = 0xFFFFu;
                                goto next_vtx;
                            }
                            aMan <<= subSh;
                            sa = (aMan << 1) & 0xFFFFFF00u;
                            prodHigh = hgMulu64h(sa, sbOOR);
                            if ((int)prodHigh < 0)
                            {
                                expProd = aExp + (int)eOOR - 126;
                                aMan = (prodHigh >> 1) & 0xFFFFFF80u;
                            }
                            else
                            {
                                expProd = aExp + (int)eOOR - 127;
                                aMan = prodHigh & 0xFFFFFF80u;
                            }
                            fsh = 141 - expProd;
                            if (fsh < 0)
                            {
                                vertex->color.final[1][3] = 0u;
                            }
                            else if (fsh > 31)
                            {
                                vertex->color.final[1][3] = 0xFFFFu;
                            }
                            else
                            {
                                unsigned int f = aMan >> fsh;
                                if (f > 0xFFFFu) f = 0xFFFFu;
                                vertex->color.final[1][3] = (unsigned short)~(unsigned short)f;
                            }
                        }
                        goto next_vtx;
                    }
                }
                vertex->color.final[1][3] = grApplyFogLINEAR_slow(state, vertex);
            next_vtx:
                vertex = &vertexBuffer[*vertexLista];
            } while (*vertexLista++ != 0xFF);
            return;
            }
        }
    }

    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->color.final[1][3] = grApplyFogLINEAR_slow(state, vertex);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grApplyFogEXP(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;
    int d;
    int da;

    GR_FETCH_CHECK(vertexList, 1943);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        d = IScalar_abs(grGetFogZ(state, vertex));
        da = IScalar_negate(IScalar_mul(d, state->fogDensity));
        vertex->color.final[1][3] =
            XScalar_tous(XScalar_sub(0x200000, IScalar_expe(da)));
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grApplyFogEXP2(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;
    int d;
    int da;

    GR_FETCH_CHECK(vertexList, 1963);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        d = IScalar_abs(grGetFogZ(state, vertex));
        da = IScalar_negate(IScalar_sqr(IScalar_mul(d, state->fogDensity)));
        vertex->color.final[1][3] =
            XScalar_tous(XScalar_sub(0x200000, IScalar_expe(da)));
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

/* --- color fetchers (fragments 1347-1748) -----------------------------------
 * Lighting path (grFetchVertexColor*): XColor3 int view at color+0..11 and
 * the shared alpha slot final[1][2] @+12. Fast path (Fast*): u16 RGBA rows
 * final[0] (lit) and final[1] (secondary). */

void grFetchVertexColorUINT8(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1487);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];
        unsigned char b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];

        XColor3_setub(&vertex->color.input.rgb[0], b0, b1, b2);
        vertex->color.final[1][2] = (unsigned short)(b3 | (b3 << 8));
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexColorFLOAT(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1514);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *p = (int *)&ptr[stride * vertex->index];

        if (grIsAligned(p, 4) == 0 && grAssert("grIsAligned(vptr, 4)", "grVertexPipeline.inl", 1521) != 0)
            grDie(0);
        XColor3_setf(&vertex->color.input.rgb[0], p[0], p[1], p[2]);
        vertex->color.final[1][2] =
            (unsigned short)hgClamp32_0_x(IScalar_scalarToFixed(IScalar_iftos(p[3]), 16), 0xFFFF);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexColorFIXED(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1542);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *vptr = (int *)&ptr[stride * vertex->index];

        if (grIsAligned(vptr, 4) == 0 && grAssert("grIsAligned(vptr, 4)", "grVertexPipeline.inl", 1545) != 0)
            grDie(0);
        XColor3_setx(&vertex->color.input.rgb[0], vptr[0], vptr[1], vptr[2]);
        vertex->color.final[1][2] = (unsigned short)hgClamp32_0_x(vptr[3], 0xFFFF);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexColorDEFAULT(Gerbera_s *state, unsigned char *vertexList)
{
    int r = state->defaultColor.r;
    int g = state->defaultColor.g;
    int b = state->defaultColor.b;
    unsigned short a = state->defaultColorRGBA[3];
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1566);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->color.input.rgb[0].r = r;
        vertex->color.input.rgb[0].g = g;
        vertex->color.input.rgb[0].b = b;
        vertex->color.final[1][2] = a;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
void grFetchVertexColorFastUINT8(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    unsigned int slot;

    GR_FETCH_CHECK(vertexList, 1336);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    slot = *vertexList++;
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if ((((uintptr_t)ptr | (unsigned int)stride) & 3u) == 0u)
    {
        do
        {
            _grVertex *vertex = &vertexBuffer[slot];
            unsigned int rgba = *(const unsigned int *)&ptr[stride * vertex->index];
            unsigned int rg, ba;
#if defined(__arm__) && !defined(__thumb__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
            unsigned int rb, ga;
            __asm__("uxtb16 %0, %2\n\t"
                    "uxtb16 %1, %2, ror #8\n\t"
                    "mov    %3, %0, lsl #8\n\t"
                    "pkhbt  %3, %3, %1, lsl #24\n\t"
                    "mov    %1, %1, lsl #8\n\t"
                    "pkhtb  %4, %1, %0, asr #8"
                    : "=&r"(rb), "=&r"(ga), "+r"(rgba), "=&r"(rg), "=&r"(ba));
#else
            rg = ((rgba & 0xFFu) << 8) | ((rgba & 0xFF00u) << 16);
            ba = ((rgba >> 8) & 0xFF00u) | (rgba & 0xFF000000u);
#endif
            __builtin_memcpy(&vertex->color.final[0][0], &rg, sizeof(rg));
            __builtin_memcpy(&vertex->color.final[0][2], &ba, sizeof(ba));
            __builtin_memcpy(&vertex->color.final[1][0], &rg, sizeof(rg));
            /* Back alpha is the fog field and must not be overwritten. */
            vertex->color.final[1][2] = (unsigned short)ba;
            slot = *vertexList++;
        } while (slot != 0xff);
        return;
    }
#endif
    do
    {
        _grVertex *vertex = &vertexBuffer[slot];
        const unsigned char *p = &ptr[stride * vertex->index];
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        /* Pack the two u16 pairs once and reuse RG for both sides. The
         * source may be unaligned or interleaved: keep byte reads. */
        unsigned int rg = ((unsigned int)p[0] << 8) | ((unsigned int)p[1] << 24);
        unsigned int ba = ((unsigned int)p[2] << 8) | ((unsigned int)p[3] << 24);
        __builtin_memcpy(&vertex->color.final[0][0], &rg, sizeof(rg));
        __builtin_memcpy(&vertex->color.final[0][2], &ba, sizeof(ba));
        __builtin_memcpy(&vertex->color.final[1][0], &rg, sizeof(rg));
        /* Back alpha is the fog field and must not be overwritten. */
        vertex->color.final[1][2] = (unsigned short)ba;
#else
        unsigned short r = (unsigned short)(p[0] << 8);
        unsigned short g = (unsigned short)(p[1] << 8);
        unsigned short b = (unsigned short)(p[2] << 8);
        unsigned short a = (unsigned short)(p[3] << 8);

        vertex->color.final[0][0] = r;
        vertex->color.final[0][1] = g;
        vertex->color.final[0][2] = b;
        vertex->color.final[1][0] = r;
        vertex->color.final[1][1] = g;
        vertex->color.final[1][2] = b;
        vertex->color.final[0][3] = a;
#endif
        slot = *vertexList++;
    } while (slot != 0xff);
}

void grFetchVertexColorFastFLOAT(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1375);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *p = (int *)&ptr[stride * vertex->index];
        unsigned short r, g, b, a;

        if (grIsAligned(p, 4) == 0 && grAssert("grIsAligned(s, 4)", "grVertexPipeline.inl", 1379) != 0)
            grDie(0);
        r = (unsigned short)(hgClamp32_0_x(IScalar_scalarToFixed(IScalar_iftos(p[0]), 16), 65280) & 0xFFFFFF00u);
        g = (unsigned short)(hgClamp32_0_x(IScalar_scalarToFixed(IScalar_iftos(p[1]), 16), 65280) & 0xFFFFFF00u);
        b = (unsigned short)(hgClamp32_0_x(IScalar_scalarToFixed(IScalar_iftos(p[2]), 16), 65280) & 0xFFFFFF00u);
        a = (unsigned short)(hgClamp32_0_x(IScalar_scalarToFixed(IScalar_iftos(p[3]), 16), 65280) & 0xFFFFFF00u);
        vertex->color.final[0][0] = r;
        vertex->color.final[0][1] = g;
        vertex->color.final[0][2] = b;
        vertex->color.final[1][0] = r;
        vertex->color.final[1][1] = g;
        vertex->color.final[1][2] = b;
        vertex->color.final[0][3] = a;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexColorFastFIXED(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *colorArray = &state->vertexArray[2];
    unsigned char *ptr = (unsigned char *)colorArray->data;
    int stride = colorArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1418);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *p = (int *)&ptr[stride * vertex->index];
        unsigned short r, g, b, a;

        if (grIsAligned(p, 4) == 0 && grAssert("grIsAligned(s, 4)", "grVertexPipeline.inl", 1422) != 0)
            grDie(0);
        r = (unsigned short)(hgClamp32_0_x(p[0], 65280) & 0xFFFFFF00u);
        g = (unsigned short)(hgClamp32_0_x(p[1], 65280) & 0xFFFFFF00u);
        b = (unsigned short)(hgClamp32_0_x(p[2], 65280) & 0xFFFFFF00u);
        a = (unsigned short)(hgClamp32_0_x(p[3], 65280) & 0xFFFFFF00u);
        vertex->color.final[0][0] = r;
        vertex->color.final[0][1] = g;
        vertex->color.final[0][2] = b;
        vertex->color.final[1][0] = r;
        vertex->color.final[1][1] = g;
        vertex->color.final[1][2] = b;
        vertex->color.final[0][3] = a;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexColorFastDEFAULT(Gerbera_s *state, unsigned char *vertexList)
{
    unsigned short r = (unsigned short)(state->defaultColorRGBA[0] & 0xFF00u);
    unsigned short g = (unsigned short)(state->defaultColorRGBA[1] & 0xFF00u);
    unsigned short b = (unsigned short)(state->defaultColorRGBA[2] & 0xFF00u);
    unsigned short a = (unsigned short)(state->defaultColorRGBA[3] & 0xFF00u);
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1461);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->color.final[0][0] = r;
        vertex->color.final[0][1] = g;
        vertex->color.final[0][2] = b;
        vertex->color.final[1][0] = r;
        vertex->color.final[1][1] = g;
        vertex->color.final[1][2] = b;
        vertex->color.final[0][3] = a;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

/* --- normal fetchers (fragments 1749-1955) ----------------------------------
 * All write the XNormal3 int triple at vertex->normal (+92). */

void grFetchVertexNormalINT8(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *normalArray = &state->vertexArray[1];
    unsigned char *ptr = (unsigned char *)normalArray->data;
    int stride = normalArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1590);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];

        XNormal3_setb(vertex->normal, (HGint8)p[0], (HGint8)p[1], (HGint8)p[2]);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexNormalINT16(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *normalArray = &state->vertexArray[1];
    unsigned char *ptr = (unsigned char *)normalArray->data;
    int stride = normalArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1613);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        short *p = (short *)&ptr[stride * vertex->index];

        XNormal3_sets(vertex->normal, p[0], p[1], p[2]);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexNormalFIXED(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *normalArray = &state->vertexArray[1];
    unsigned char *ptr = (unsigned char *)normalArray->data;
    int stride = normalArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1636);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *vptr = (int *)&ptr[stride * vertex->index];

        if (grIsAligned(vptr, 4) == 0 && grAssert("grIsAligned(vptr, 4)", "grVertexPipeline.inl", 1639) != 0)
            grDie(0);
        XNormal3_setx(vertex->normal, vptr[0], vptr[1], vptr[2]);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexNormalFLOAT(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *normalArray = &state->vertexArray[1];
    unsigned char *ptr = (unsigned char *)normalArray->data;
    int stride = normalArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1661);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        int *vptr = (int *)&ptr[stride * vertex->index];

        if (grIsAligned(vptr, 4) == 0 && grAssert("grIsAligned(vptr, 4)", "grVertexPipeline.inl", 1664) != 0)
            grDie(0);
        XNormal3_setf(vertex->normal, *(HGfloat *)&vptr[0], *(HGfloat *)&vptr[1], *(HGfloat *)&vptr[2]);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchVertexNormalDEFAULT(Gerbera_s *state, unsigned char *vertexList)
{
    int x = state->defaultNormal[0];
    int y = state->defaultNormal[1];
    int z = state->defaultNormal[2];
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1684);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->normal[0] = x;
        vertex->normal[1] = y;
        vertex->normal[2] = z;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

/* --- texture coordinate transformers (fragments 1956-2253) ------------------
 * Each walks the vertex list, transforms the raw texcoord through
 * state->texCoordTransform[texUnit] and stores the 10.21 (x,y) result at
 * vertex->texCoord[texUnit] (+56 + 8*texUnit). */

void grTransformTexCoordINT8x2(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1708);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];
        int st[2];

        st[0] = (int)(signed char)p[0] * 65536;
        st[1] = (int)(signed char)p[1] * 65536;
        ITCTransform_mulxv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit], st, 2);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grTransformTexCoordINT8x4(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    int size = array->size;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1736);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];
        int strq[4];

        strq[0] = (int)(signed char)p[0] * 65536;
        strq[1] = (int)(signed char)p[1] * 65536;
        strq[2] = (int)(signed char)p[2] * 65536;
        if (size == 4)
            strq[3] = (int)(signed char)p[3] * 65536;
        ITCTransform_mulxv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit], strq, size);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grTransformTexCoordINT16x2(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1766);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];
        int st[2];

        st[0] = (int)*(short *)(p + 0) * 65536;
        st[1] = (int)*(short *)(p + 2) * 65536;
        ITCTransform_mulxv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit], st, 2);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grTransformTexCoordINT16x4(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    int size = array->size;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1793);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        unsigned char *p = &ptr[stride * vertex->index];
        int strq[4];

        strq[0] = (int)*(short *)(p + 0) * 65536;
        strq[1] = (int)*(short *)(p + 2) * 65536;
        strq[2] = (int)*(short *)(p + 4) * 65536;
        if (size == 4)
            strq[3] = (int)*(short *)(p + 6) * 65536;
        ITCTransform_mulxv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit], strq, size);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grTransformTexCoordFIXEDx4(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    int size = array->size;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1823);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        ITCTransform_mulxv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit],
                           (int *)&ptr[stride * vertex->index], size);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grTransformTexCoordDEFAULT(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    int tcX = state->defaultTexCoord.x[2][2 * texUnit];
    int tcY = state->defaultTexCoord.x[2][2 * texUnit + 1];
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1843);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->texCoord[texUnit].x = tcX;
        vertex->texCoord[texUnit].y = tcY;
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("O3")))
#endif
void grTransformTexCoordFLOATx4(Gerbera_s *state, unsigned char *vertexList, int texUnit)
{
    _grVertexArray *array = &state->vertexArray[6 + texUnit];
    unsigned char *ptr = (unsigned char *)array->data;
    int stride = array->stride;
    int size = array->size;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1866);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    if (__builtin_expect(size == 2 && (unsigned int)state->texCoordTransform[texUnit].cl == 0x80200802u, 1))
    {
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && !defined(__thumb__)
        register unsigned char *vbuf __asm__("r4") = (unsigned char *)vertexBuffer;
        register unsigned char *v __asm__("r5") = (unsigned char *)vertex;
        register unsigned char *p __asm__("r6") = ptr;
        register int st __asm__("r7") = stride;
        register unsigned char *vl __asm__("r8") = vertexLista;
        register int toff __asm__("r9") = (int)((const unsigned char *)&vertexBuffer[0].texCoord[texUnit] - (const unsigned char *)vertexBuffer);
        __asm__ volatile(
            "1:\n\t"
            "ldr   r0, [%[v]]\n\t"
            "mla   ip, r0, %[st], %[p]\n\t"
            "ldmia ip, {r2, r3}\n\t"
            "mov   r0, r2, lsl #1\n\t"
            "mov   r0, r0, lsr #24\n\t"
            "rsbs  r1, r0, #132\n\t"
            "blt   2f\n\t"
            "orr   ip, r2, #0x00800000\n\t"
            "mov   ip, ip, lsl #8\n\t"
            "mov   ip, ip, lsr #5\n\t"
            "cmp   r2, #0\n\t"
            "rsbmi ip, ip, #0\n\t"
            "cmp   r0, #0\n\t"
            "moveq ip, #0\n\t"
            "mov   r2, ip, asr r1\n\t"
            "3:\n\t"
            "mov   r0, r3, lsl #1\n\t"
            "mov   r0, r0, lsr #24\n\t"
            "rsbs  r1, r0, #132\n\t"
            "blt   4f\n\t"
            "orr   ip, r3, #0x00800000\n\t"
            "mov   ip, ip, lsl #8\n\t"
            "mov   ip, ip, lsr #5\n\t"
            "cmp   r3, #0\n\t"
            "rsbmi ip, ip, #0\n\t"
            "cmp   r0, #0\n\t"
            "moveq ip, #0\n\t"
            "mov   r3, ip, asr r1\n\t"
            "5:\n\t"
            "add   %[v], %[v], %[toff]\n\t"
            "stmia %[v], {r2, r3}\n\t"
            "ldrb  r0, [%[vl]], #1\n\t"
            "cmp   r0, #255\n\t"
            "addne %[v], %[vbuf], r0, lsl #7\n\t"
            "bne   1b\n\t"
            "b     9f\n\t"
            "2:\n\t"
            "cmp   r0, #255\n\t"
            "moveq r2, #0\n\t"
            "beq   3b\n\t"
            "orr   ip, r2, #0x00800000\n\t"
            "mov   ip, ip, lsl #8\n\t"
            "mov   ip, ip, lsr #5\n\t"
            "cmp   r2, #0\n\t"
            "rsbmi ip, ip, #0\n\t"
            "rsb   r1, r1, #0\n\t"
            "cmp   r1, #31\n\t"
            "movgt r1, #31\n\t"
            "mov   r2, ip, lsl r1\n\t"
            "b     3b\n\t"
            "4:\n\t"
            "cmp   r0, #255\n\t"
            "moveq r3, #0\n\t"
            "beq   5b\n\t"
            "orr   ip, r3, #0x00800000\n\t"
            "mov   ip, ip, lsl #8\n\t"
            "mov   ip, ip, lsr #5\n\t"
            "cmp   r3, #0\n\t"
            "rsbmi ip, ip, #0\n\t"
            "rsb   r1, r1, #0\n\t"
            "cmp   r1, #31\n\t"
            "movgt r1, #31\n\t"
            "mov   r3, ip, lsl r1\n\t"
            "b     5b\n\t"
            "9:\n\t"
            : [v] "+r"(v), [vl] "+r"(vl)
            : [vbuf] "r"(vbuf), [p] "r"(p), [st] "r"(st), [toff] "r"(toff)
            : "r0", "r1", "r2", "r3", "ip", "cc", "memory");
#else
        do
        {
            const int *src = (const int *)&ptr[stride * vertex->index];
            int s0, s1;
            if (grIsAligned(src, 4) == 0 &&
                grAssert("grIsAligned(ptr + stride*vertex->index, 4)", "grVertexPipeline.inl", 1868) != 0)
                grDie(0);
            s0 = src[0];
            s1 = src[1];
            vertex->texCoord[texUnit].x = grTexCoordUnitFloatFast(s0);
            vertex->texCoord[texUnit].y = grTexCoordUnitFloatFast(s1);
            vertex = &vertexBuffer[*vertexLista];
        } while (*vertexLista++ != 0xFF);
#endif
        return;
    }
    do
    {
        if (grIsAligned(&ptr[stride * vertex->index], 4) == 0 &&
            grAssert("grIsAligned(ptr + stride*vertex->index, 4)", "grVertexPipeline.inl", 1868) != 0)
            grDie(0);
        ITCTransform_mulfv(&vertex->texCoord[texUnit].x,
                           &state->texCoordTransform[texUnit],
                           (int *)&ptr[stride * vertex->index], size);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

/* --- point size fetchers (fragments 2416-2489) ------------------------------ */

void grFetchPointSizeFLOAT(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *sizeArray = &state->vertexArray[5];
    unsigned char *ptr = (unsigned char *)sizeArray->data;
    int stride = sizeArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 1990);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        float *p = (float *)&ptr[stride * vertex->index];
        int s = IScalar_scalarToFixed(IScalar_ftos(*p), 16);

        vertex->pointSize = hgClamp32(s, 0x10000, 0x400000);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

void grFetchPointSizeFIXED(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertexArray *sizeArray = &state->vertexArray[5];
    unsigned char *base = (unsigned char *)sizeArray->data;
    int stride = sizeArray->stride;
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;

    GR_FETCH_CHECK(vertexList, 2013);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        vertex->pointSize = hgClamp32(*(int *)&base[stride * vertex->index], 0x10000, 0x400000);
        vertex = &vertexBuffer[*vertexLista];
    } while (*vertexLista++ != 0xFF);
}

/* --- grUpdateVertexPipeline (decompile 30378-30651; fragment assert lines
 * 2122/2149/2170/2194/2237/2268/2294) ----------------------------------------
 * Refreshes everything the fetch stages consume: texture transforms +
 * default texcoords, light records, normal transforms, and the stage
 * function pointers (color/normal/texcoord/point-size fetch, fog). */

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void grUpdateVertexPipeline(Gerbera_s *state)
{
    int transformVE;
    int computePointDistAttn;
    int u;
    int i;

    if (state->primitiveType == 0)   /* raw comparison in the decompile */
    {
        computePointDistAttn = (IFVector3_is100(&state->pointDistAttn) == 0);
        state->vertexPipelineDirty = (computePointDistAttn != state->computePointDistAttn);
        state->computePointDistAttn = (unsigned char)computePointDistAttn;
    }
    for (u = 1; u >= 0; --u)
    {
        _grVertexArray *array = &state->vertexArray[6 + u];

        if (grIsMatrixDirty(state, GR_MATRIXMODE_TEXTURE0 + u) != 0)
        {
            grSetMatrixDirty(state, GR_MATRIXMODE_TEXTURE0 + u, 0);
            ITCTransform_copy(&state->texCoordTransform[u],
                              &state->matrix[GR_MATRIXMODE_TEXTURE0 + u]);
        }
        if (state->textureUnitEnabled[u] != 0 && array->data == NULL)
            ITCTransform_mulfv(&state->defaultTexCoord.x[2][2 * u],
                               &state->texCoordTransform[u],
                               (int *)state->defaultTexCoord.f[u], 4);
    }
    transformVE = state->computePointDistAttn;
    if (grTestCapability(state, GR_CAPABILITY_LIGHTING) != 0)
    {
        int rescale = grTestCapability(state, GR_CAPABILITY_RESCALE_NORMAL);

        transformVE = grUpdateLights(state);
        if (state->normalsToEyeDirty != 0)
        {
            IMatrix4x4_inversetranspose(&state->normalsToEyeTransform,
                                        &state->matrix[GR_MATRIXMODE_MODELVIEW], rescale);
            state->normalsToEyeDirty = 0;
        }
        if (grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) != 0)
        {
            for (i = 8; i >= 0; --i)
            {
                if (state->normalsToEyePalDirty[i] != 0)
                {
                    IMatrix4x4_inversetranspose(&state->normalsToEyePal[i],
                                                &state->matrix[GR_MATRIXMODE_MATRIXPALETTE0 + i], rescale);
                    state->normalsToEyePalDirty[i] = 0;
                }
            }
        }
    }
    if (state->vertexPipelineDirty == 0)
    {
        if (!(state->transformVE == transformVE) && grAssert("state->transformVE == transformVE", "grVertexPipeline.inl", 2122) != 0)
            grDie(0);
    }
    state->transformVE = (unsigned char)transformVE;

    /* color fetch stage */
    if (grTestCapability(state, GR_CAPABILITY_LIGHTING) != 0)
    {
        if (grTestCapability(state, GR_CAPABILITY_COLOR_MATERIAL) != 0)
        {
            if (grTestCapability(state, GR_CAPABILITY_COLOR_ARRAY) != 0 &&
                state->vertexArray[2].data != NULL)
            {
                switch (state->vertexArray[2].type)
                {
                    case GR_DATATYPE_UINT8:
                        state->fetchVertexColor = grFetchVertexColorUINT8;
                        break;
                    case GR_DATATYPE_FIXED:
                        state->fetchVertexColor = grFetchVertexColorFIXED;
                        break;
                    default:
                        if (state->vertexArray[2].type != GR_DATATYPE_FLOAT &&
                            grAssert("color_array->type == GR_DATATYPE_FLOAT", "grVertexPipeline.inl", 2149) != 0)
                            grDie(0);
                        state->fetchVertexColor = grFetchVertexColorFLOAT;
                        break;
                }
            }
            else
            {
                state->fetchVertexColor = grFetchVertexColorDEFAULT;
            }
        }
        else
        {
            state->fetchVertexColor = NULL;
        }
    }
    else if (grTestCapability(state, GR_CAPABILITY_COLOR_ARRAY) != 0 &&
             state->vertexArray[2].data != NULL)
    {
        switch (state->vertexArray[2].type)
        {
            case GR_DATATYPE_UINT8:
                state->fetchVertexColor = grFetchVertexColorFastUINT8;
                break;
            case GR_DATATYPE_FIXED:
                state->fetchVertexColor = grFetchVertexColorFastFIXED;
                break;
            default:
                if (state->vertexArray[2].type != GR_DATATYPE_FLOAT &&
                    grAssert("color_array->type == GR_DATATYPE_FLOAT", "grVertexPipeline.inl", 2170) != 0)
                    grDie(0);
                state->fetchVertexColor = grFetchVertexColorFastFLOAT;
                break;
        }
    }
    else
    {
        state->fetchVertexColor = grFetchVertexColorFastDEFAULT;
    }

    /* normal fetch stage */
    if (grTestCapability(state, GR_CAPABILITY_LIGHTING) != 0)
    {
        /* Both original binaries use a bound pointer even if NORMAL_ARRAY is disabled. */
        if (state->vertexArray[1].data != NULL)
        {
            switch (state->vertexArray[1].type)
            {
                case GR_DATATYPE_INT8:
                    state->fetchVertexNormal = grFetchVertexNormalINT8;
                    break;
                case GR_DATATYPE_INT16:
                    state->fetchVertexNormal = grFetchVertexNormalINT16;
                    break;
                case GR_DATATYPE_FIXED:
                    state->fetchVertexNormal = grFetchVertexNormalFIXED;
                    break;
                default:
                    if (state->vertexArray[1].type != GR_DATATYPE_FLOAT &&
                        grAssert("normal_array->type == GR_DATATYPE_FLOAT", "grVertexPipeline.inl", 2194) != 0)
                        grDie(0);
                    state->fetchVertexNormal = grFetchVertexNormalFLOAT;
                    break;
            }
        }
        else
        {
            state->fetchVertexNormal = grFetchVertexNormalDEFAULT;
        }
    }
    else
    {
        state->fetchVertexNormal = NULL;
    }

    /* texcoord fetch stages */
    for (u = 0; u < 2; ++u)
    {
        _grVertexArray *array = &state->vertexArray[6 + u];

        if (state->textureUnitEnabled[u] != 0)
        {
            if (array->data != NULL &&
                grTestCapability(state, GR_CAPABILITY_TEXTURE_COORD_ARRAY_0 + u) != 0)
            {
                switch (array->type)
                {
                    case GR_DATATYPE_INT16:
                        if (array->size == 2)
                            state->fetchTexCoord[u] = grTransformTexCoordINT16x2;
                        else
                            state->fetchTexCoord[u] = grTransformTexCoordINT16x4;
                        break;
                    case GR_DATATYPE_FLOAT:
                        state->fetchTexCoord[u] = grTransformTexCoordFLOATx4;
                        break;
                    case GR_DATATYPE_FIXED:
                        state->fetchTexCoord[u] = grTransformTexCoordFIXEDx4;
                        break;
                    default:
                        if (array->type != GR_DATATYPE_INT8 &&
                            grAssert("type == GR_DATATYPE_INT8", "grVertexPipeline.inl", 2237) != 0)
                            grDie(0);
                        if (array->size == 2)
                            state->fetchTexCoord[u] = grTransformTexCoordINT8x2;
                        else
                            state->fetchTexCoord[u] = grTransformTexCoordINT8x4;
                        break;
                }
            }
            else
            {
                state->fetchTexCoord[u] = grTransformTexCoordDEFAULT;
            }
        }
        else
        {
            state->fetchTexCoord[u] = NULL;
        }
    }

    /* point size fetch stage */
    if (grGetPrimitiveVertices(state->primitiveType) == GR_PRIMITIVETYPE_POINT &&
        grTestCapability(state, GR_CAPABILITY_POINT_SMOOTH) == 0 &&
        grTestCapability(state, GR_CAPABILITY_POINT_SIZE_ARRAY) != 0 &&
        state->vertexArray[5].data != NULL)
    {
        if (state->vertexArray[5].type == GR_DATATYPE_FLOAT)
        {
            state->fetchPointSize = grFetchPointSizeFLOAT;
        }
        else
        {
            if (state->vertexArray[5].type != GR_DATATYPE_FIXED &&
                grAssert("sizeArray->type == GR_DATATYPE_FIXED", "grVertexPipeline.inl", 2268) != 0)
                grDie(0);
            state->fetchPointSize = grFetchPointSizeFIXED;
        }
    }
    else
    {
        state->fetchPointSize = NULL;
    }

    /* fog stage */
    if (grTestCapability(state, GR_CAPABILITY_FOG) != 0 && (state->colorMask & 7) != 0)
    {
        if (state->fogMode != GR_FOGMODE_LINEAR)
        {
            if (state->fogMode == GR_FOGMODE_EXP)
            {
                state->applyFog = grApplyFogEXP;
            }
            else
            {
                if (state->fogMode != GR_FOGMODE_EXP2 &&
                    grAssert("state->fogMode == GR_FOGMODE_EXP2", "grVertexPipeline.inl", 2294) != 0)
                    grDie(0);
                state->applyFog = grApplyFogEXP2;
            }
        }
        else if (IScalar_testeq(state->fogOORange) != 0)
        {
            state->applyFog = grApplyFogFAST;
        }
        else
        {
            state->applyFog = grApplyFogLINEAR;
        }
    }
    else
    {
        state->applyFog = NULL;
    }
}

/* --- position state / clip codes (decompile 27797-27862) -------------------- */

void grSetVertexPositionState(_grVertex *v, int state)
{
    v->stateMask = (unsigned short)((v->stateMask & 0x3FFF) | (state << 14));
}

int grIsOutsideUserClipPlane(_IFVector4 *plane, _IVector4 *clipPos)
{
    _IFVector4 normalized;
    int sign;
    int dot;

    sign = IFVector4_getFirstNonZeroSign(plane);
    if (sign >= 0)
        IFVector4_copy(&normalized, plane);
    else
        IFVector4_negate(&normalized, plane);
    dot = IScalar_mul(IFVector4_getf(&normalized, 0), IVector4_getf(clipPos, 0));
    dot = IScalar_add(dot, IScalar_mul(IFVector4_getf(&normalized, 1), IVector4_getf(clipPos, 1)));
    dot = IScalar_add(dot, IScalar_mul(IFVector4_getf(&normalized, 2), IVector4_getf(clipPos, 2)));
    dot = IScalar_add(dot, IScalar_mul(IFVector4_getf(&normalized, 3), IVector4_getf(clipPos, 3)));
    if (sign < 0)
        dot = IScalar_negate(dot);
    return IScalar_testlt(dot);
}

#if defined(__GNUC__) || defined(__clang__)
static unsigned int grComputeUserClipCodes(Gerbera_s *state, _grVertex *vertex, unsigned int clipMask)
#if defined(__arm__) && !defined(__thumb__)
    __asm__("grComputeUserClipCodes_asm")
#endif
    ;
__attribute__((noinline, used))
#endif
static unsigned int grComputeUserClipCodes(Gerbera_s *state, _grVertex *vertex, unsigned int clipMask)
{
    unsigned int i;

    for (i = (unsigned int)state->enabledUserClipPlaneIndices; i != 0; i >>= 4)
        clipMask |= (unsigned int)grIsOutsideUserClipPlane(
                        &state->userClipPlanes[(i & 0xF) - 1],
                        &vertex->clipPosition)
                    << ((i & 0xF) + 9);
    return clipMask;
}

unsigned int grComputeVertexClipCodes(Gerbera_s *state, _grVertex *vertex)
{
    int x = vertex->clipPosition.m[0];
    int y = vertex->clipPosition.m[1];
    int z = vertex->clipPosition.m[2];
    int w = vertex->clipPosition.m[3];
    unsigned int clipMask;

    if ((x | y | z | w) != 0)
    {
        clipMask =
            ((unsigned int)(w + z) >> 31) |
            (((unsigned int)(w - z) >> 31) << 1) |
            (((unsigned int)(w + x) >> 31) << 6) |
            (((unsigned int)(w - x) >> 31) << 7) |
            (((unsigned int)(w + y) >> 31) << 8) |
            (((unsigned int)(w - y) >> 31) << 9);
        if (clipMask != 0u)
        {
            clipMask |=
                (((unsigned int)(w + (x >> 2)) >> 31) << 2) |
                (((unsigned int)(w - (x >> 2)) >> 31) << 3) |
                (((unsigned int)(w + (y >> 2)) >> 31) << 4) |
                (((unsigned int)(w - (y >> 2)) >> 31) << 5);
        }
    }
    else
    {
        clipMask = 15u;
    }
    if (state->enabledUserClipPlaneIndices != 0)
        clipMask = grComputeUserClipCodes(state, vertex, clipMask);
    vertex->clipMask = (unsigned short)clipMask;
    return clipMask;
}

/* --- grFetchWeightAndIndex (decompile 27862-27923; asserts grVertexPipeline
 * .inl 155/164/168/181/198/206) ----------------------------------------------
 * Reads the matrix-palette weight array (vertexArray[3]) and the packed
 * matrix indices (vertexArray[4]); indices are clamped to 8 (9 palette
 * matrices) and packed one byte per weight slot. */

unsigned int grFetchWeightAndIndex(Gerbera_s *state, int *weights, int vertexIndex)
{
    _grVertexArray *weightArray = &state->vertexArray[3];
    _grVertexArray *palIndexArray = &state->vertexArray[4];
    unsigned char *wsrc;
    unsigned char *isrc;
    unsigned int indices = 0;
    int i;

    if (!(state != NULL) && grAssert("state", "grVertexPipeline.inl", 155) != 0)
        grDie(0);
    if (!(weightArray->data != NULL) && grAssert("weightArray->data", "grVertexPipeline.inl", 164) != 0)
        grDie(0);
    wsrc = (unsigned char *)weightArray->data + weightArray->stride * vertexIndex;
    if (!(hgInRange32(weightArray->size, 1, 4) != 0) && grAssert("hgInRange32(weightArray->size, 1, 4)", "grVertexPipeline.inl", 168) != 0)
        grDie(0);
    if (weightArray->type == GR_DATATYPE_FLOAT)
    {
        for (i = weightArray->size - 1; i >= 0; --i)
            weights[i] = IScalar_iftos(((int *)wsrc)[i]);
    }
    else
    {
        if (!(weightArray->type == GR_DATATYPE_FIXED) && grAssert("weightArray->type == GR_DATATYPE_FIXED", "grVertexPipeline.inl", 181) != 0)
            grDie(0);
        for (i = weightArray->size - 1; i >= 0; --i)
            weights[i] = IScalar_fixedToScalar(((int *)wsrc)[i], 16);
    }
    if (!(palIndexArray->data != NULL) && grAssert("palIndexArray->data", "grVertexPipeline.inl", 198) != 0)
        grDie(0);
    isrc = (unsigned char *)palIndexArray->data + palIndexArray->stride * vertexIndex;
    if (!(palIndexArray->type == GR_DATATYPE_UINT8) && grAssert("palIndexArray->type == GR_DATATYPE_UINT8", "grVertexPipeline.inl", 206) != 0)
        grDie(0);
    for (i = palIndexArray->size - 1; i >= 0; --i)
        indices = (indices << 8) | hgMinu32(isrc[i], 8u);
    return indices;
}

/* --- grTransformVertex (decompile 27926-28117; asserts grVertexPipeline.inl
 * 230/231/232/246/250/259/270/279/280/348/358) --------------------------------
 * Fetches the vertex position (float or fixed), applies the matrix-palette
 * skinning when active, produces the clip position and the clip codes. */

void grTransformVertex(Gerbera_s *state, _grVertex *vertex)
{
    grTransformVertexPreloaded(state, vertex, 0);
}

/* Keep 64-bit matrix products close to their consumers to limit ARM register spills. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && defined(__ARM_ARCH) && __ARM_ARCH >= 6
__attribute__((optimize("no-schedule-insns")))
#endif
static void grTransformVertexGeneral(Gerbera_s *state, _grVertex *vertex, int weightsPreloaded)
{
    _grVertexArray *array = &state->vertexArray[0];
    unsigned char *src;
    int ok = 1;

    if (!(state != NULL && vertex != NULL) && grAssert("state && vertex", "grVertexPipeline.inl", 230) != 0)
        ok = 0;
    if (ok == 0)
        grDie(0);
    if (!(array->data != NULL) && grAssert("array->data", "grVertexPipeline.inl", 231) != 0)
        grDie(0);
    if (!((vertex->stateMask & GR_VERTEXMASK_TRANSFORMED) == 0) &&
        grAssert("!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED)", "grVertexPipeline.inl", 232) != 0)
        grDie(0);
    vertex->stateMask |= GR_VERTEXMASK_TRANSFORMED;
    src = (unsigned char *)array->data + array->stride * vertex->index;
    if (!(hgInRange32(array->size, 2, 4) != 0) && grAssert("hgInRange32(array->size, 2, 4)", "grVertexPipeline.inl", 246) != 0)
        grDie(0);
    if (array->type == GR_DATATYPE_FLOAT)
    {
        if (grIsAligned(src, 4) == 0 && grAssert("grIsAligned(src, 4)", "grVertexPipeline.inl", 250) != 0)
            grDie(0);
        IFVector4_setfv(vertex->position.objectSpaceFloat, (int *)src, array->size);
        grSetVertexPositionState(vertex, GR_POSITION_OBJECT_SPACE_FLOAT);
    }
    else
    {
        int x, y = 0x10000, z = 0, w = 0x10000;
        int size = array->size;

        if (!(hgInRange32(size, 2, 4) != 0) && grAssert("hgInRange32(size, 2, 4)", "grVertexPipeline.inl", 259) != 0)
            grDie(0);
        if (array->type == GR_DATATYPE_INT16)
        {
            if (grIsAligned(src, 2) == 0 && grAssert("grIsAligned(src, 2)", "grVertexPipeline.inl", 270) != 0)
                grDie(0);
            x = (int)*(short *)(src + 0) * 65536;
            y = (int)*(short *)(src + 2) * 65536;
            if (size >= 3)
                z = (int)*(short *)(src + 4) * 65536;
            if (size > 3)
                w = (int)*(short *)(src + 6) * 65536;
        }
        else if (array->type != GR_DATATYPE_INT8)
        {
            if (!(array->type == GR_DATATYPE_FIXED) && grAssert("array->type == GR_DATATYPE_FIXED", "grVertexPipeline.inl", 279) != 0)
                grDie(0);
            if (grIsAligned(src, 4) == 0 && grAssert("grIsAligned(src, 4)", "grVertexPipeline.inl", 280) != 0)
                grDie(0);
            x = *(int *)(src + 0);
            y = *(int *)(src + 4);
            if (size >= 3)
                z = *(int *)(src + 8);
            if (size > 3)
                w = *(int *)(src + 12);
        }
        else
        {
            x = (int)(signed char)src[0] * 65536;
            y = (int)(signed char)src[1] * 65536;
            if (size >= 3)
                z = (int)(signed char)src[2] * 65536;
            if (size > 3)
                w = (int)(signed char)src[3] * 65536;
        }
        /* the fixed-point position is consumed as the int view {x,y,z,w}
         * (16.16): the float view's e slot holds x */
        vertex->position.objectSpaceFloat[0].e = (unsigned int)x;
        vertex->position.objectSpaceFloat[0].m[0] = y;
        vertex->position.objectSpaceFloat[0].m[1] = z;
        vertex->position.objectSpaceFloat[0].m[2] = w;
        grSetVertexPositionState(vertex, GR_POSITION_OBJECT_SPACE_FIXED);
    }
    if (grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) != 0 &&
        grTestCapability(state, GR_CAPABILITY_MATRIX_INDEX_ARRAY) != 0 &&
        grTestCapability(state, GR_CAPABILITY_WEIGHT_ARRAY) != 0 &&
        state->vertexArray[3].data != NULL &&
        state->vertexArray[4].data != NULL)
    {
        int localWeights[4];
        int *weights;
        unsigned int weightIndices;
        int count;
        int j;
        _IFVector4 tmp[4];

        if (weightsPreloaded != 0)
        {
            weights = vertex->weights;
            weightIndices = vertex->weightIndices;
        }
        else
        {
            weightIndices = grFetchWeightAndIndex(state, localWeights, vertex->index);
            weights = localWeights;
        }
        count = hgMin32(state->vertexArray[3].size, state->vertexArray[4].size);
        count = hgMin32(count, 4);
        if (grGetVertexPositionState(vertex) != GR_POSITION_OBJECT_SPACE_FLOAT)
        {
            for (j = 0; j < count; ++j)
            {
                if (IScalar_testne(weights[j]) != 0)
                    IVertexTransform_mulxv4s(&tmp[j],
                                             &state->matrixPalTransform[weightIndices & 0xFF],
                                             (int *)&vertex->position);
                weightIndices >>= 8;
            }
        }
        else
        {
            for (j = 0; j < count; ++j)
            {
                if (IScalar_testne(weights[j]) != 0)
                    IVertexTransform_mulfv4s(&tmp[j],
                                             &state->matrixPalTransform[weightIndices & 0xFF],
                                             vertex->position.objectSpaceFloat);
                weightIndices >>= 8;
            }
        }
        IFVector4_accfv(vertex->position.objectSpaceFloat, tmp, weights, count);
        grSetVertexPositionState(vertex, GR_POSITION_EYE_SPACE);
        IVertexTransform_mulfv4(&vertex->clipPosition,
                                &state->projectionTransform,
                                vertex->position.objectSpaceFloat);
        if (IVector4_isValid(&vertex->clipPosition) == 0 &&
            grAssert("IVector4_isValid(vertex->clipPosition)", "grVertexPipeline.inl", 348) != 0)
            grDie(0);
    }
    else if (grGetVertexPositionState(vertex) != GR_POSITION_OBJECT_SPACE_FLOAT)
    {
        if (!(grGetVertexPositionState(vertex) == GR_POSITION_OBJECT_SPACE_FIXED) &&
            grAssert("grGetVertexPositionState(vertex) == GR_POSITION_OBJECT_SPACE_FIXED", "grVertexPipeline.inl", 358) != 0)
            grDie(0);
        IVertexTransform_mulxv4(&vertex->clipPosition,
                                &state->objectToClipTransform,
                                (int *)&vertex->position);
    }
    else
    {
        _IFVector4 *position = vertex->position.objectSpaceFloat;
        if (state->renderStackAllocated && !state->enabledUserClipPlaneIndices &&
            IVertexTransform_mulfv4Common(&vertex->clipPosition, &state->commonTransform, position))
        {
            vertex->stateMask |= GR_VERTEXMASK_APPROXIMATE_POSITION;
            goto transformed;
        }
        /* Shared exponent patterns reuse alignment work; stop caching after
         * consecutive misses in a draw with little exponent locality. */
        if (state->renderStackAllocated && state->floatTransformMisses < GR_FLOAT_TRANSFORM_MISS_LIMIT &&
            state->objectToClipTransform.cl != 0x80200802u)
        {
            unsigned int recent = state->floatTransformRecent;
            _IFVertexTransformPlan *plan = &state->floatTransformPlan[recent];
            if (plan->inputExponent != position->e)
            {
                recent ^= 1;
                plan = &state->floatTransformPlan[recent];
                state->floatTransformRecent = recent;
                if (plan->inputExponent != position->e)
                {
                    ++state->floatTransformMisses;
                    IVertexTransform_prepareFloat(plan, &state->objectToClipTransform, position->e);
                }
                else
                    state->floatTransformMisses = 0;
            }
            else
                state->floatTransformMisses = 0;
            IVertexTransform_mulfv4Prepared(&vertex->clipPosition,
                                            &state->objectToClipTransform, position, plan);
        }
        else
            IVertexTransform_mulfv4(&vertex->clipPosition,
                                    &state->objectToClipTransform, position);
    }
transformed:
    grComputeVertexClipCodes(state, vertex);
}

static inline int grVp_dot4hi(const int *q, int m0, int m1, int m2, int m3)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    unsigned int lo;
    int hi;
    __asm__("smull %0, %1, %2, %3" : "=&r"(lo), "=&r"(hi) : "r"(q[0]), "r"(m0));
    __asm__("smlal %0, %1, %4, %5" : "=&r"(lo), "=&r"(hi) : "0"(lo), "1"(hi), "r"(q[1]), "r"(m1));
    __asm__("smlal %0, %1, %4, %5" : "=&r"(lo), "=&r"(hi) : "0"(lo), "1"(hi), "r"(q[2]), "r"(m2));
    __asm__("smlal %0, %1, %4, %5" : "=&r"(lo), "=&r"(hi) : "0"(lo), "1"(hi), "r"(q[3]), "r"(m3));
    return hi;
#else
    return (int)(((HGint64)q[0] * m0 + (HGint64)q[1] * m1 +
                  (HGint64)q[2] * m2 + (HGint64)q[3] * m3) >> 32);
#endif
}

#if defined(__GNUC__) || defined(__clang__)
static void grTransformVertexFloat3Fallback(Gerbera_s *state, _grVertex *vertex, const int *src)
    __asm__("grTransformVertexFloat3Fallback_asm") __attribute__((noinline, used));
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void grTransformVertexFloat3Fallback(Gerbera_s *state, _grVertex *vertex, const int *src)
{
    _IFVector4 *position = vertex->position.objectSpaceFloat;
    IFVector4_setfv(position, (int *)src, 3);
    grSetVertexPositionState(vertex, GR_POSITION_OBJECT_SPACE_FLOAT);
    vertex->stateMask |= GR_VERTEXMASK_TRANSFORMED;
    if (state->floatTransformMisses < GR_FLOAT_TRANSFORM_MISS_LIMIT &&
        state->objectToClipTransform.cl != 0x80200802u)
    {
        unsigned int recent = state->floatTransformRecent;
        _IFVertexTransformPlan *fplan = &state->floatTransformPlan[recent];
        if (fplan->inputExponent != position->e)
        {
            recent ^= 1;
            fplan = &state->floatTransformPlan[recent];
            state->floatTransformRecent = recent;
            if (fplan->inputExponent != position->e)
            {
                ++state->floatTransformMisses;
                IVertexTransform_prepareFloat(fplan, &state->objectToClipTransform, position->e);
            }
            else
                state->floatTransformMisses = 0;
        }
        else
            state->floatTransformMisses = 0;
        IVertexTransform_mulfv4Prepared(&vertex->clipPosition,
                                        &state->objectToClipTransform, position, fplan);
    }
    else
        IVertexTransform_mulfv4(&vertex->clipPosition,
                                &state->objectToClipTransform, position);
    grComputeVertexClipCodes(state, vertex);
}

#if defined(__GNUC__) && defined(__arm__) && !defined(__thumb__) && \
    (defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_5TE__) || \
     defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || \
     (defined(__ARM_ARCH) && __ARM_ARCH >= 5)) && \
    !HG_ASSERT_ENABLED
typedef char gr_check_arm_vp_offsets_[
    (offsetof(Gerbera_s, vertexArray[0].stride) == 5404 &&
     offsetof(Gerbera_s, vertexArray[0].data) == 5412 &&
     offsetof(Gerbera_s, commonTransform) == 7940 &&
     offsetof(_ICommonVertexTransform, exponent) == 64 &&
     offsetof(_grVertex, index) == 0 &&
     offsetof(_grVertex, stateMask) == 4 &&
     offsetof(_grVertex, clipMask) == 6 &&
     offsetof(_grVertex, clipPosition) == 8 &&
     IM_COMMON_TRANSFORM_SPREAD == 16 &&
     IM_COMMON_TRANSFORM_MIN_W == 0x20000 &&
     IM_COMMON_TRANSFORM_CLIP_MARGIN == 32 &&
     GR_VERTEXMASK_TRANSFORMED == 0x4 &&
     GR_VERTEXMASK_APPROXIMATE_POSITION == 0x100) ? 1 : -1];

__attribute__((naked, noinline))
static void grTransformVertexFloat3Common(Gerbera_s *state, _grVertex *vertex)
{
    (void)state;
    (void)vertex;
    __asm__ __volatile__(
        "stmfd sp!, {r0, r4-r11, lr}\n\t"
        "add   r2, r0, #4096\n\t"
        "ldr   r3, [r2, #1308]\n\t"
        "ldr   ip, [r2, #1316]\n\t"
        "ldr   r4, [r1, #0]\n\t"
        "mla   r2, r3, r4, ip\n\t"
        "ldmia r2, {r3, r4, r5}\n\t"
        "mov   r6, #255\n\t"
        "and   r7, r6, r3, lsr #23\n\t"
        "and   r8, r6, r4, lsr #23\n\t"
        "and   r9, r6, r5, lsr #23\n\t"
        "mov   r10, #127\n\t"
        "cmp   r10, r7\n\t"
        "movcc r10, r7\n\t"
        "cmp   r10, r8\n\t"
        "movcc r10, r8\n\t"
        "cmp   r10, r9\n\t"
        "movcc r10, r9\n\t"
        "sub   r6, r10, #127\n\t"
        "cmp   r6, #16\n\t"
        "bhi   1f\n\t"

        "cmp   r7, #0\n\t"
        "moveq r2, #0\n\t"
        "beq   2f\n\t"
        "sub   r11, r10, r7\n\t"
        "cmp   r11, #16\n\t"
        "bhi   1f\n\t"
        "orr   r2, r3, #0x00800000\n\t"
        "mov   r2, r2, lsl #8\n\t"
        "mov   r2, r2, lsr #2\n\t"
        "cmp   r3, #0\n\t"
        "rsbmi r2, r2, #0\n\t"
        "mov   r2, r2, asr r11\n\t"
    "2:\n\t"
        "cmp   r8, #0\n\t"
        "moveq r3, #0\n\t"
        "beq   3f\n\t"
        "sub   r11, r10, r8\n\t"
        "cmp   r11, #16\n\t"
        "bhi   1f\n\t"
        "orr   r3, r4, #0x00800000\n\t"
        "mov   r3, r3, lsl #8\n\t"
        "mov   r3, r3, lsr #2\n\t"
        "cmp   r4, #0\n\t"
        "rsbmi r3, r3, #0\n\t"
        "mov   r3, r3, asr r11\n\t"
    "3:\n\t"
        "cmp   r9, #0\n\t"
        "moveq r4, #0\n\t"
        "beq   4f\n\t"
        "sub   r11, r10, r9\n\t"
        "cmp   r11, #16\n\t"
        "bhi   1f\n\t"
        "orr   r4, r5, #0x00800000\n\t"
        "mov   r4, r4, lsl #8\n\t"
        "mov   r4, r4, lsr #2\n\t"
        "cmp   r5, #0\n\t"
        "rsbmi r4, r4, #0\n\t"
        "mov   r4, r4, asr r11\n\t"
    "4:\n\t"
        "mov   r5, #0x20000000\n\t"
        "mov   r5, r5, lsr r6\n\t"

        "add   r8, r0, #7936\n\t"
        "add   r8, r8, #4\n\t"
        "add   r12, r8, #48\n\t"
        "ldmia r12, {r6, r9, r10, r11}\n\t"
        "smull lr, r7, r6, r2\n\t"
        "smlal lr, r7, r9, r3\n\t"
        "smlal lr, r7, r10, r4\n\t"
        "smlal lr, r7, r11, r5\n\t"
        "cmp   r7, #0x20000\n\t"
        "blt   1f\n\t"

        "ldmia r8!, {r0, r6, r10, r11}\n\t"
        "smull r12, r9, r0, r2\n\t"
        "smlal r12, r9, r6, r3\n\t"
        "smlal r12, r9, r10, r4\n\t"
        "smlal r12, r9, r11, r5\n\t"
        "eor   r6, r9, r9, asr #31\n\t"
        "sub   r6, r6, r9, asr #31\n\t"
        "sub   r11, r7, #32\n\t"
        "cmp   r11, r6, asr #2\n\t"
        "ble   1f\n\t"
        "sub   r11, r6, r11\n\t"
        "cmp   r11, #64\n\t"
        "blo   1f\n\t"
        "orr   r0, r7, r6\n\t"

        "ldmia r8!, {r6, r11}\n\t"
        "smull r12, r10, r6, r2\n\t"
        "smlal r12, r10, r11, r3\n\t"
        "ldmia r8!, {r6, r11}\n\t"
        "smlal r12, r10, r6, r4\n\t"
        "smlal r12, r10, r11, r5\n\t"
        "eor   r6, r10, r10, asr #31\n\t"
        "sub   r6, r6, r10, asr #31\n\t"
        "sub   r11, r7, #32\n\t"
        "cmp   r11, r6, asr #2\n\t"
        "ble   1f\n\t"
        "sub   r11, r6, r11\n\t"
        "cmp   r11, #64\n\t"
        "blo   1f\n\t"
        "orr   r0, r0, r6\n\t"

        "ldmia r8!, {r6, r11}\n\t"
        "smull r12, lr, r6, r2\n\t"
        "smlal r12, lr, r11, r3\n\t"
        "ldmia r8, {r6, r11}\n\t"
        "smlal r12, lr, r6, r4\n\t"
        "smlal r12, lr, r11, r5\n\t"
        "eor   r11, lr, lr, asr #31\n\t"
        "sub   r11, r11, lr, asr #31\n\t"
        "sub   r12, r11, r7\n\t"
        "add   r12, r12, #32\n\t"
        "cmp   r12, #64\n\t"
        "blo   1f\n\t"

        "clz   r6, r5\n\t"
        "orr   r2, r0, r11\n\t"
        "clz   r2, r2\n\t"
        "sub   r2, r2, #2\n\t"
        "ldr   r3, [r8, #24]\n\t"
        "add   r3, r3, r6\n\t"
        "add   r3, r3, #1\n\t"
        "sub   r3, r3, r2\n\t"
        "sub   r4, r3, #2\n\t"
        "cmp   r4, #252\n\t"
        "bhs   1f\n\t"

        "mov   r4, #0\n\t"
        "adds  r5, r7, lr\n\t"
        "orrmi r4, r4, #1\n\t"
        "subs  r5, r7, lr\n\t"
        "orrmi r4, r4, #2\n\t"
        "adds  r5, r7, r9\n\t"
        "orrmi r4, r4, #64\n\t"
        "subs  r5, r7, r9\n\t"
        "orrmi r4, r4, #128\n\t"
        "adds  r5, r7, r10\n\t"
        "orrmi r4, r4, #256\n\t"
        "subs  r5, r7, r10\n\t"
        "orrmi r4, r4, #512\n\t"
        "ldrh  r5, [r1, #4]\n\t"
        "bic   r5, r5, #0xC000\n\t"
        "orr   r5, r5, #0x104\n\t"
        "strh  r5, [r1, #4]\n\t"
        "strh  r4, [r1, #6]\n\t"
        "mov   r9, r9, lsl r2\n\t"
        "mov   r10, r10, lsl r2\n\t"
        "mov   r11, lr, lsl r2\n\t"
        "mov   r12, r7, lsl r2\n\t"
        "add   r1, r1, #8\n\t"
        "stmia r1, {r3, r9, r10, r11, r12}\n\t"
        "add   sp, sp, #4\n\t"
        "ldmfd sp!, {r4-r11, pc}\n\t"

    "1:\n\t"
        "ldr   r0, [sp, #0]\n\t"
        "add   r2, r0, #4096\n\t"
        "ldr   r3, [r2, #1308]\n\t"
        "ldr   ip, [r2, #1316]\n\t"
        "ldr   r2, [r1, #0]\n\t"
        "mla   r2, r3, r2, ip\n\t"
        "ldmfd sp!, {r0, r4-r11, lr}\n\t"
        "b     grTransformVertexFloat3Fallback_asm\n\t"
    );
}
#else
static void grTransformVertexFloat3Common(Gerbera_s *state, _grVertex *vertex)
{
    const _grVertexArray *array = &state->vertexArray[0];
    const int *src;
    int v0, v1, v2;
    unsigned int e0, e1, e2, maxE;
    int m0, m1, m2, m3;
    const _ICommonVertexTransform *plan;
    int r0, r1, r2, r3;
    int a0, a1, a2;
    unsigned int probe;
    int shift, exponent;

    GR_VP_CHECK(state && vertex, 230);
    GR_VP_CHECK(array->data, 231);
    GR_VP_CHECK(!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED), 232);
    src = (const int *)((const unsigned char *)array->data + array->stride * vertex->index);
    GR_VP_CHECK(grIsAligned(src, 4), 250);

    v0 = src[0];
    v1 = src[1];
    v2 = src[2];
    e0 = (unsigned int)(unsigned char)((unsigned int)v0 >> 23);
    e1 = (unsigned int)(unsigned char)((unsigned int)v1 >> 23);
    e2 = (unsigned int)(unsigned char)((unsigned int)v2 >> 23);
    if (e0 == 255u || e1 == 255u || e2 == 255u)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }

    maxE = 127u;
    if (maxE < e0) maxE = e0;
    if (maxE < e1) maxE = e1;
    if (maxE < e2) maxE = e2;
    if ((e0 && maxE - e0 > IM_COMMON_TRANSFORM_SPREAD) ||
        (e1 && maxE - e1 > IM_COMMON_TRANSFORM_SPREAD) ||
        (e2 && maxE - e2 > IM_COMMON_TRANSFORM_SPREAD) ||
        (maxE - 127u > IM_COMMON_TRANSFORM_SPREAD))
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }

    {
        unsigned int s0 = (unsigned int)(v0 >> 31);
        unsigned int s1 = (unsigned int)(v1 >> 31);
        unsigned int s2 = (unsigned int)(v2 >> 31);
        unsigned int mag0 = e0 ? ((((unsigned int)v0 & 0x7FFFFFu) | 0x800000u) << 6) : 0u;
        unsigned int mag1 = e1 ? ((((unsigned int)v1 & 0x7FFFFFu) | 0x800000u) << 6) : 0u;
        unsigned int mag2 = e2 ? ((((unsigned int)v2 & 0x7FFFFFu) | 0x800000u) << 6) : 0u;
        m0 = e0 ? ((int)((mag0 ^ s0) - s0) >> (maxE - e0)) : 0;
        m1 = e1 ? ((int)((mag1 ^ s1) - s1) >> (maxE - e1)) : 0;
        m2 = e2 ? ((int)((mag2 ^ s2) - s2) >> (maxE - e2)) : 0;
        m3 = 0x20000000 >> (maxE - 127u);
    }

    plan = &state->commonTransform;
    r3 = grVp_dot4hi(plan->m[3], m0, m1, m2, m3);
    if (r3 < IM_COMMON_TRANSFORM_MIN_W)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }
    probe = (unsigned int)r3;
    r0 = grVp_dot4hi(plan->m[0], m0, m1, m2, m3);
    a0 = hgAbs32(r0);
    if ((a0 >> 2) >= r3 - IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (unsigned int)(a0 - r3 + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }
    probe |= (unsigned int)a0;
    r1 = grVp_dot4hi(plan->m[1], m0, m1, m2, m3);
    a1 = hgAbs32(r1);
    if ((a1 >> 2) >= r3 - IM_COMMON_TRANSFORM_CLIP_MARGIN ||
        (unsigned int)(a1 - r3 + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }
    probe |= (unsigned int)a1;
    r2 = grVp_dot4hi(plan->m[2], m0, m1, m2, m3);
    a2 = hgAbs32(r2);
    if ((unsigned int)(a2 - r3 + IM_COMMON_TRANSFORM_CLIP_MARGIN) < 2 * IM_COMMON_TRANSFORM_CLIP_MARGIN)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }
    probe |= (unsigned int)a2;
    shift = hgClz32(probe) - 2;
    exponent = plan->exponent + (int)maxE - 124 - shift;
    if ((unsigned int)(exponent - 2) >= 252)
    {
        grTransformVertexFloat3Fallback(state, vertex, src);
        return;
    }

    vertex->stateMask = (unsigned short)((vertex->stateMask & 0x3FFFu) |
                        GR_VERTEXMASK_TRANSFORMED | GR_VERTEXMASK_APPROXIMATE_POSITION);
    vertex->clipMask = (unsigned short)(
        ((unsigned int)(r3 + r2) >> 31) |
        (((unsigned int)(r3 - r2) >> 31) << 1) |
        (((unsigned int)(r3 + r0) >> 31) << 6) |
        (((unsigned int)(r3 - r0) >> 31) << 7) |
        (((unsigned int)(r3 + r1) >> 31) << 8) |
        (((unsigned int)(r3 - r1) >> 31) << 9));
    vertex->clipPosition.e = (unsigned int)exponent;
    vertex->clipPosition.m[0] = (int)((unsigned int)r0 << shift);
    vertex->clipPosition.m[1] = (int)((unsigned int)r1 << shift);
    vertex->clipPosition.m[2] = (int)((unsigned int)r2 << shift);
    vertex->clipPosition.m[3] = (int)((unsigned int)r3 << shift);
}
#endif

static inline HGint64 grVp_mulacc3ShRow(HGint64 w, const int *u, unsigned int sh,
                                          int s0, int s1, int s2)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    unsigned int lo = (unsigned int)(HGuint64)w;
    int hi = (int)((HGuint64)w >> 32);
    int t0, t1;
    __asm__(
        "and %2, %7, #255\n\t"
        "rsb %2, %2, #16\n\t"
        "ldr %3, [%6, #0]\n\t"
        "lsl %2, %8, %2\n\t"
        "smlal %0, %1, %3, %2\n\t"
        "lsr %2, %7, #8\n\t"
        "and %2, %2, #255\n\t"
        "rsb %2, %2, #16\n\t"
        "ldr %3, [%6, #4]\n\t"
        "lsl %2, %9, %2\n\t"
        "smlal %0, %1, %3, %2\n\t"
        "lsr %2, %7, #16\n\t"
        "and %2, %2, #255\n\t"
        "rsb %2, %2, #16\n\t"
        "ldr %3, [%6, #8]\n\t"
        "lsl %2, %10, %2\n\t"
        "smlal %0, %1, %3, %2"
        : "=&r"(lo), "=&r"(hi), "=&r"(t0), "=&r"(t1)
        : "0"(lo), "1"(hi), "r"(u), "r"(sh), "r"(s0), "r"(s1), "r"(s2),
          "m"(*(const int (*)[3])u));
    return (HGint64)(((HGuint64)(unsigned int)hi << 32) | lo);
#else
    int x0 = (int)((unsigned int)s0 << (16 - (sh & 0xFFu)));
    int x1 = (int)((unsigned int)s1 << (16 - ((sh >> 8) & 0xFFu)));
    int x2 = (int)((unsigned int)s2 << (16 - ((sh >> 16) & 0xFFu)));
    return w + (HGint64)u[0] * x0 + (HGint64)u[1] * x1 + (HGint64)u[2] * x2;
#endif
}

static inline HGint64 grVp_mulacc3ShRowGeneral(HGint64 w, const int *u, unsigned int sh,
                                               int s0, int s1, int s2)
{
#if (defined(__GNUC__) || defined(__clang__)) && defined(__arm__) && !defined(__thumb__)
    unsigned int lo = (unsigned int)(HGuint64)w;
    int hi = (int)((HGuint64)w >> 32);
    int t0, t1, t2, t3;
    __asm__(
        "and   %2, %7, #255\n\t"
        "rsbs  %2, %2, #16\n\t"
        "ldr   %3, [%6, #0]\n\t"
        "blt   10f\n\t"
        "mov   %2, %8, lsl %2\n\t"
        "smlal %0, %1, %3, %2\n\t"
        "b     11f\n\t"
    "10:\n\t"
        "rsb   %2, %2, #0\n\t"
        "smull %4, %5, %3, %8\n\t"
        "rsb   %3, %2, #32\n\t"
        "mov   %4, %4, lsr %2\n\t"
        "orr   %4, %4, %5, lsl %3\n\t"
        "adds  %0, %0, %4\n\t"
        "adc   %1, %1, %5, asr %2\n\t"
    "11:\n\t"
        "mov   %2, %7, lsr #8\n\t"
        "and   %2, %2, #255\n\t"
        "rsbs  %2, %2, #16\n\t"
        "ldr   %3, [%6, #4]\n\t"
        "blt   12f\n\t"
        "mov   %2, %9, lsl %2\n\t"
        "smlal %0, %1, %3, %2\n\t"
        "b     13f\n\t"
    "12:\n\t"
        "rsb   %2, %2, #0\n\t"
        "smull %4, %5, %3, %9\n\t"
        "rsb   %3, %2, #32\n\t"
        "mov   %4, %4, lsr %2\n\t"
        "orr   %4, %4, %5, lsl %3\n\t"
        "adds  %0, %0, %4\n\t"
        "adc   %1, %1, %5, asr %2\n\t"
    "13:\n\t"
        "mov   %2, %7, lsr #16\n\t"
        "and   %2, %2, #255\n\t"
        "rsbs  %2, %2, #16\n\t"
        "ldr   %3, [%6, #8]\n\t"
        "blt   14f\n\t"
        "mov   %2, %10, lsl %2\n\t"
        "smlal %0, %1, %3, %2\n\t"
        "b     15f\n\t"
    "14:\n\t"
        "rsb   %2, %2, #0\n\t"
        "smull %4, %5, %3, %10\n\t"
        "rsb   %3, %2, #32\n\t"
        "mov   %4, %4, lsr %2\n\t"
        "orr   %4, %4, %5, lsl %3\n\t"
        "adds  %0, %0, %4\n\t"
        "adc   %1, %1, %5, asr %2\n\t"
    "15:"
        : "+&r"(lo), "+&r"(hi), "=&r"(t0), "=&r"(t1), "=&r"(t2), "=&r"(t3)
        : "r"(u), "r"(sh), "r"(s0), "r"(s1), "r"(s2), "m"(*(const int (*)[3])u)
        : "cc");
    return (HGint64)(((HGuint64)(unsigned int)hi << 32) | lo);
#else
    int sh0 = (int)(sh & 0xFFu);
    int sh1 = (int)((sh >> 8) & 0xFFu);
    int sh2 = (int)((sh >> 16) & 0xFFu);
    w += (sh0 <= 16) ? hgMul64(u[0], (int)((unsigned int)s0 << (16 - sh0)))
                     : (hgMul64(u[0], s0) >> (sh0 - 16));
    w += (sh1 <= 16) ? hgMul64(u[1], (int)((unsigned int)s1 << (16 - sh1)))
                     : (hgMul64(u[1], s1) >> (sh1 - 16));
    w += (sh2 <= 16) ? hgMul64(u[2], (int)((unsigned int)s2 << (16 - sh2)))
                     : (hgMul64(u[2], s2) >> (sh2 - 16));
    return w;
#endif
}

#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
typedef char gr_check_arm_s3_tc_[offsetof(Gerbera_s, objectToClipTransform) == 1416 ? 1 : -1];
typedef char gr_check_arm_s3_uc_[offsetof(Gerbera_s, enabledUserClipPlaneIndices) == 5000 ? 1 : -1];
typedef char gr_check_arm_s3_st_[offsetof(Gerbera_s, vertexArray[0].stride) == 5404 ? 1 : -1];
typedef char gr_check_arm_s3_dt_[offsetof(Gerbera_s, vertexArray[0].data) == 5412 ? 1 : -1];
typedef char gr_check_arm_s3_tw_[offsetof(_IVertexTransform, w) == 80 ? 1 : -1];
typedef char gr_check_arm_s3_tu_[offsetof(_IVertexTransform, unnormalized) == 112 ? 1 : -1];
typedef char gr_check_arm_s3_ts_[offsetof(_IVertexTransform, sh) == 176 ? 1 : -1];
typedef char gr_check_arm_s3_te_[offsetof(_IVertexTransform, exp) == 192 ? 1 : -1];
typedef char gr_check_arm_s3_tl_[offsetof(_IVertexTransform, cl) == 196 ? 1 : -1];
typedef char gr_check_arm_s3_vi_[offsetof(_grVertex, index) == 0 ? 1 : -1];
typedef char gr_check_arm_s3_vm_[offsetof(_grVertex, stateMask) == 4 ? 1 : -1];
typedef char gr_check_arm_s3_vc_[offsetof(_grVertex, clipMask) == 6 ? 1 : -1];
typedef char gr_check_arm_s3_vp_[offsetof(_grVertex, clipPosition) == 8 ? 1 : -1];
typedef char gr_check_arm_s3_vo_[offsetof(_grVertex, position) == 72 ? 1 : -1];
typedef char gr_check_arm_s3_pf_[GR_POSITION_OBJECT_SPACE_FIXED == 1 ? 1 : -1];
typedef char gr_check_arm_s3_vt_[GR_VERTEXMASK_TRANSFORMED == 4 ? 1 : -1];

static void grTransformVertexShort3Identity(Gerbera_s *state, _grVertex *vertex)
    __asm__("grTransformVertexShort3Identity_asm");
static __attribute__((noinline, used)) void grTransformVertexShort3Identity(Gerbera_s *state, _grVertex *vertex)
{
    _grVertexArray *array = &state->vertexArray[0];
    const short *src;
    int *position = (int *)&vertex->position;
    int x, y, z, w = 65536;
    unsigned int amax;
    int sh, e;

    src = (const short *)((const unsigned char *)array->data + array->stride * vertex->index);
    vertex->stateMask = (unsigned short)((vertex->stateMask & 0x3FFFu) |
                        (GR_POSITION_OBJECT_SPACE_FIXED << 14) | GR_VERTEXMASK_TRANSFORMED);
    x = (int)((unsigned int)src[0] << 16);
    y = (int)((unsigned int)src[1] << 16);
    z = (int)((unsigned int)src[2] << 16);
    position[0] = x;
    position[1] = y;
    position[2] = z;
    position[3] = 65536;

    amax = (unsigned int)(hgAbs32(x) | hgAbs32(y) | hgAbs32(z) | 0x10000);
    sh = hgClz32(amax) - 2;
    e = 140 - sh;
    if (sh >= 0)
    {
        x = (int)((unsigned int)x << sh);
        y = (int)((unsigned int)y << sh);
        z = (int)((unsigned int)z << sh);
        w = (int)((unsigned int)w << sh);
    }
    else
    {
        sh = -sh;
        x >>= sh; y >>= sh; z >>= sh; w >>= sh;
    }
    if (x == -1073741824 || y == -1073741824 ||
        z == -1073741824 || w == -1073741824)
    {
        x >>= 1; y >>= 1; z >>= 1; w >>= 1;
        ++e;
    }
    IVector4_set_noINF(&vertex->clipPosition, e, x, y, z, w);
    grComputeVertexClipCodes(state, vertex);
}

#define GR_ARM_S3_COMMON_PREAMBLE \
    "stmfd sp!, {r0, r1, r2, r4-r11, lr}\n\t" \
    "add   r3, r0, #4096\n\t" \
    "ldr   r4, [r3, #1308]\n\t" \
    "ldr   ip, [r1, #0]\n\t" \
    "ldr   r3, [r3, #1316]\n\t" \
    "mla   r2, r4, ip, r3\n\t" \
    "ldrh  r4, [r1, #4]\n\t" \
    "bic   r4, r4, #0xC000\n\t" \
    "orr   r4, r4, #0x4000\n\t" \
    "orr   r4, r4, #4\n\t" \
    "strh  r4, [r1, #4]\n\t" \
    "add   lr, r0, #1408\n\t" \
    "add   r11, r1, #72\n\t" \
    "ldrsh r0, [r2, #0]\n\t" \
    "ldrsh r1, [r2, #2]\n\t" \
    "ldrsh r2, [r2, #4]\n\t" \
    "mov   r7, r0, lsl #16\n\t" \
    "mov   r8, r1, lsl #16\n\t" \
    "mov   r9, r2, lsl #16\n\t" \
    "mov   r10, #0x10000\n\t" \
    "stmia r11, {r7, r8, r9, r10}\n\t" \
    "add   ip, lr, #88\n\t" \
    "ldmia ip, {r4-r11}\n\t"

#define GR_ARM_S3_COL_PREAMBLE \
    "stmfd sp!, {r0, r1, r2, r4-r11, lr}\n\t" \
    "add   r3, r0, #4096\n\t" \
    "ldr   r4, [r3, #1308]\n\t" \
    "ldr   ip, [r1, #0]\n\t" \
    "ldr   r3, [r3, #1316]\n\t" \
    "mla   r2, r4, ip, r3\n\t" \
    "ldrh  r4, [r1, #4]\n\t" \
    "bic   r4, r4, #0xC000\n\t" \
    "orr   r4, r4, #0x4000\n\t" \
    "orr   r4, r4, #4\n\t" \
    "strh  r4, [r1, #4]\n\t" \
    "add   lr, r0, #1408\n\t" \
    "add   r11, r1, #72\n\t" \
    "ldrsh r0, [r2, #0]\n\t" \
    "ldrsh r1, [r2, #2]\n\t" \
    "ldrsh r6, [r2, #4]\n\t" \
    "mov   r7, r0, lsl #16\n\t" \
    "mov   r8, r1, lsl #16\n\t" \
    "mov   r9, r6, lsl #16\n\t" \
    "mov   r10, #0x10000\n\t" \
    "stmia r11, {r7, r8, r9, r10}\n\t" \
    "str   r6, [sp, #8]\n\t" \
    "add   ip, lr, #88\n\t" \
    "ldmia ip, {r4-r11}\n\t"

#define GR_ARM_S3_FAST16_ELEM(rowOff, colOff, rS, rLo, rHi) \
    "ldrb  ip, [lr, #" #rowOff "]\n\t" \
    "ldr   r3, [lr, #" #colOff "]\n\t" \
    "rsb   ip, ip, #16\n\t" \
    "mov   ip, " #rS ", lsl ip\n\t" \
    "smlal " #rLo ", " #rHi ", r3, ip\n\t"

#define GR_ARM_S3_SLOW16_ELEM(rowOff, colOff, rS, rTmp, rLo, rHi) \
    "ldrb  ip, [lr, #" #rowOff "]\n\t" \
    "ldr   r3, [lr, #" #colOff "]\n\t" \
    "sub   ip, ip, #16\n\t" \
    "smull r3, " #rTmp ", " #rS ", r3\n\t" \
    "mov   r3, r3, lsr ip\n\t" \
    "rsb   ip, ip, #32\n\t" \
    "orr   r3, r3, " #rTmp ", lsl ip\n\t" \
    "rsb   ip, ip, #32\n\t" \
    "adds  " #rLo ", " #rLo ", r3\n\t" \
    "adc   " #rHi ", " #rHi ", " #rTmp ", asr ip\n\t"

#define GR_ARM_S3_SLOW16_ELEM2(rowOff, colOff, rS, rTmp, rInv, rLo, rHi) \
    "ldrb  ip, [lr, #" #rowOff "]\n\t" \
    "ldr   r3, [lr, #" #colOff "]\n\t" \
    "sub   ip, ip, #16\n\t" \
    "smull r3, " #rTmp ", " #rS ", r3\n\t" \
    "mov   r3, r3, lsr ip\n\t" \
    "rsb   " #rInv ", ip, #32\n\t" \
    "orr   r3, r3, " #rTmp ", lsl " #rInv "\n\t" \
    "adds  " #rLo ", " #rLo ", r3\n\t" \
    "adc   " #rHi ", " #rHi ", " #rTmp ", asr ip\n\t"

#define GR_ARM_S3_GEN_ELEM(rowOff, colOff, rS, rLo, rHi, lblSlow, lblCont) \
    "ldrb  ip, [lr, #" #rowOff "]\n\t" \
    "ldr   r3, [lr, #" #colOff "]\n\t" \
    "rsbs  ip, ip, #16\n\t" \
    "blt   " #lblSlow "f\n\t" \
    "mov   ip, " #rS ", lsl ip\n\t" \
    "smlal " #rLo ", " #rHi ", r3, ip\n\t" \
    #lblCont ":\n\t"

#define GR_ARM_S3_GEN_SLOW(rS, rTmp, rLo, rHi, lblSlow, lblCont) \
    #lblSlow ":\n\t" \
    "rsb   ip, ip, #0\n\t" \
    "smull r3, " #rTmp ", " #rS ", r3\n\t" \
    "mov   r3, r3, lsr ip\n\t" \
    "rsb   ip, ip, #32\n\t" \
    "orr   r3, r3, " #rTmp ", lsl ip\n\t" \
    "rsb   ip, ip, #32\n\t" \
    "adds  " #rLo ", " #rLo ", r3\n\t" \
    "adc   " #rHi ", " #rHi ", " #rTmp ", asr ip\n\t" \
    "b     " #lblCont "b\n\t"

#define GR_ARM_S3_GEN_SLOW2(rS, rTmp, rInv, rLo, rHi, lblSlow, lblCont) \
    #lblSlow ":\n\t" \
    "rsb   ip, ip, #0\n\t" \
    "smull r3, " #rTmp ", " #rS ", r3\n\t" \
    "mov   r3, r3, lsr ip\n\t" \
    "rsb   " #rInv ", ip, #32\n\t" \
    "orr   r3, r3, " #rTmp ", lsl " #rInv "\n\t" \
    "adds  " #rLo ", " #rLo ", r3\n\t" \
    "adc   " #rHi ", " #rHi ", " #rTmp ", asr ip\n\t" \
    "b     " #lblCont "b\n\t"

#define GR_ARM_S3_COMMON_TAIL \
    "mov   ip, r5, asr #31\n\t" \
    "eor   r0, r4, ip\n\t" \
    "eor   r1, r5, ip\n\t" \
    "subs  r0, r0, ip\n\t" \
    "sbc   r1, r1, ip\n\t" \
    "mov   ip, r7, asr #31\n\t" \
    "eor   r2, r6, ip\n\t" \
    "eor   r3, r7, ip\n\t" \
    "subs  r2, r2, ip\n\t" \
    "sbc   r3, r3, ip\n\t" \
    "orr   r0, r0, r2\n\t" \
    "orr   r1, r1, r3\n\t" \
    "mov   ip, r9, asr #31\n\t" \
    "eor   r2, r8, ip\n\t" \
    "eor   r3, r9, ip\n\t" \
    "subs  r2, r2, ip\n\t" \
    "sbc   r3, r3, ip\n\t" \
    "orr   r0, r0, r2\n\t" \
    "orr   r1, r1, r3\n\t" \
    "mov   ip, r11, asr #31\n\t" \
    "eor   r2, r10, ip\n\t" \
    "eor   r3, r11, ip\n\t" \
    "subs  r2, r2, ip\n\t" \
    "sbc   r3, r3, ip\n\t" \
    "orr   r0, r0, r2\n\t" \
    "orr   r1, r1, r3\n\t" \
    "orrs  r2, r0, r1\n\t" \
    "beq   2f\n\t" \
    "subs  r2, r1, #0\n\t" \
    "movne r3, #0\n\t" \
    "moveq r3, #32\n\t" \
    "moveq r2, r0\n\t" \
    "clz   r2, r2\n\t" \
    "add   r2, r2, r3\n\t" \
    "ldr   r3, [lr, #200]\n\t" \
    "subs  r2, r2, #2\n\t" \
    "bmi   10f\n\t" \
    "sub   r3, r3, r2\n\t" \
    "cmp   r2, #32\n\t" \
    "bge   3f\n\t" \
    "rsb   ip, r2, #32\n\t" \
    "mov   r5, r5, lsl r2\n\t" \
    "orr   r4, r5, r4, lsr ip\n\t" \
    "mov   r7, r7, lsl r2\n\t" \
    "orr   r6, r7, r6, lsr ip\n\t" \
    "mov   r9, r9, lsl r2\n\t" \
    "orr   r8, r9, r8, lsr ip\n\t" \
    "mov   r11, r11, lsl r2\n\t" \
    "orr   r10, r11, r10, lsr ip\n\t" \
    "4:\n\t" \
    "cmp   r4, #-1073741824\n\t" \
    "cmpne r6, #-1073741824\n\t" \
    "cmpne r8, #-1073741824\n\t" \
    "cmpne r10, #-1073741824\n\t" \
    "bne   5f\n\t" \
    "mov   r4, r4, asr #1\n\t" \
    "mov   r6, r6, asr #1\n\t" \
    "mov   r8, r8, asr #1\n\t" \
    "mov   r10, r10, asr #1\n\t" \
    "add   r3, r3, #1\n\t" \
    "5:\n\t" \
    "cmp   r3, #0\n\t" \
    "ble   2f\n\t" \
    "cmp   r3, #254\n\t" \
    "bgt   6f\n\t" \
    "7:\n\t" \
    "ldr   r1, [sp, #4]\n\t" \
    "add   r2, r1, #8\n\t" \
    "stmia r2, {r3, r4, r6, r8, r10}\n\t" \
    "mov   r2, #0\n\t" \
    "adds  ip, r10, r8\n\t" \
    "orrmi r2, r2, #1\n\t" \
    "subs  ip, r10, r8\n\t" \
    "orrmi r2, r2, #2\n\t" \
    "adds  ip, r10, r4\n\t" \
    "orrmi r2, r2, #64\n\t" \
    "subs  ip, r10, r4\n\t" \
    "orrmi r2, r2, #128\n\t" \
    "adds  ip, r10, r6\n\t" \
    "orrmi r2, r2, #256\n\t" \
    "subs  ip, r10, r6\n\t" \
    "orrmi r2, r2, #512\n\t" \
    "cmp   r2, #0\n\t" \
    "beq   8f\n\t" \
    "adds  ip, r10, r4, asr #2\n\t" \
    "orrmi r2, r2, #4\n\t" \
    "subs  ip, r10, r4, asr #2\n\t" \
    "orrmi r2, r2, #8\n\t" \
    "adds  ip, r10, r6, asr #2\n\t" \
    "orrmi r2, r2, #16\n\t" \
    "subs  ip, r10, r6, asr #2\n\t" \
    "orrmi r2, r2, #32\n\t" \
    "8:\n\t" \
    "ldr   r0, [sp, #0]\n\t" \
    "add   ip, r0, #4096\n\t" \
    "ldr   r3, [ip, #904]\n\t" \
    "cmp   r3, #0\n\t" \
    "bne   9f\n\t" \
    "strh  r2, [r1, #6]\n\t" \
    "ldmfd sp!, {r0, r1, r2, r4-r11, pc}\n\t" \
    "10:\n\t" \
    "sub   r3, r3, r2\n\t" \
    "mov   r4, r5, asr #1\n\t" \
    "mov   r6, r7, asr #1\n\t" \
    "mov   r8, r9, asr #1\n\t" \
    "mov   r10, r11, asr #1\n\t" \
    "b     4b\n\t" \
    "3:\n\t" \
    "sub   r2, r2, #32\n\t" \
    "mov   r4, r4, lsl r2\n\t" \
    "mov   r6, r6, lsl r2\n\t" \
    "mov   r8, r8, lsl r2\n\t" \
    "mov   r10, r10, lsl r2\n\t" \
    "b     4b\n\t" \
    "2:\n\t" \
    "ldr   r1, [sp, #4]\n\t" \
    "mov   r3, #0\n\t" \
    "mov   r4, #0\n\t" \
    "mov   r6, #0\n\t" \
    "mov   r8, #0\n\t" \
    "mov   r10, #0\n\t" \
    "add   r2, r1, #8\n\t" \
    "stmia r2, {r3, r4, r6, r8, r10}\n\t" \
    "mov   r2, #15\n\t" \
    "b     8b\n\t" \
    "6:\n\t" \
    "mov   r3, #254\n\t" \
    "cmp   r4, #0\n\t" \
    "mvnge r4, #0xC0000000\n\t" \
    "movlt r4, #-0x3FFFFFFF\n\t" \
    "cmp   r6, #0\n\t" \
    "mvnge r6, #0xC0000000\n\t" \
    "movlt r6, #-0x3FFFFFFF\n\t" \
    "cmp   r8, #0\n\t" \
    "mvnge r8, #0xC0000000\n\t" \
    "movlt r8, #-0x3FFFFFFF\n\t" \
    "cmp   r10, #0\n\t" \
    "mvnge r10, #0xC0000000\n\t" \
    "movlt r10, #-0x3FFFFFFF\n\t" \
    "b     7b\n\t" \
    "9:\n\t" \
    "bl    grComputeUserClipCodes_asm\n\t" \
    "ldr   r1, [sp, #4]\n\t" \
    "strh  r0, [r1, #6]\n\t" \
    "ldmfd sp!, {r0, r1, r2, r4-r11, pc}\n\t"

__attribute__((naked))
static void grTransformVertexShort3Fast16(Gerbera_s *state, _grVertex *vertex)
{
    (void)state;
    (void)vertex;
    __asm__ __volatile__(
        GR_ARM_S3_COMMON_PREAMBLE
        GR_ARM_S3_FAST16_ELEM(184, 120, r0, r4, r5)
        GR_ARM_S3_FAST16_ELEM(185, 124, r1, r4, r5)
        GR_ARM_S3_FAST16_ELEM(186, 128, r2, r4, r5)
        GR_ARM_S3_FAST16_ELEM(188, 136, r0, r6, r7)
        GR_ARM_S3_FAST16_ELEM(189, 140, r1, r6, r7)
        GR_ARM_S3_FAST16_ELEM(190, 144, r2, r6, r7)
        GR_ARM_S3_FAST16_ELEM(192, 152, r0, r8, r9)
        GR_ARM_S3_FAST16_ELEM(193, 156, r1, r8, r9)
        GR_ARM_S3_FAST16_ELEM(194, 160, r2, r8, r9)
        "ldr   r3, [lr, #204]\n\t"
        "tst   r3, #0x3F000000\n\t"
        "beq   1f\n\t"
        GR_ARM_S3_FAST16_ELEM(196, 168, r0, r10, r11)
        GR_ARM_S3_FAST16_ELEM(197, 172, r1, r10, r11)
        GR_ARM_S3_FAST16_ELEM(198, 176, r2, r10, r11)
    "1:\n\t"
        GR_ARM_S3_COMMON_TAIL
    );
}

__attribute__((naked))
static void grTransformVertexShort3Slow16(Gerbera_s *state, _grVertex *vertex)
{
    (void)state;
    (void)vertex;
    __asm__ __volatile__(
        GR_ARM_S3_COL_PREAMBLE
        GR_ARM_S3_SLOW16_ELEM(184, 120, r0, r2, r4, r5)
        GR_ARM_S3_SLOW16_ELEM(188, 136, r0, r2, r6, r7)
        GR_ARM_S3_SLOW16_ELEM(192, 152, r0, r2, r8, r9)
        GR_ARM_S3_SLOW16_ELEM(196, 168, r0, r2, r10, r11)
        GR_ARM_S3_SLOW16_ELEM2(185, 124, r1, r2, r0, r4, r5)
        GR_ARM_S3_SLOW16_ELEM2(189, 140, r1, r2, r0, r6, r7)
        GR_ARM_S3_SLOW16_ELEM2(193, 156, r1, r2, r0, r8, r9)
        GR_ARM_S3_SLOW16_ELEM2(197, 172, r1, r2, r0, r10, r11)
        "ldr   r2, [sp, #8]\n\t"
        GR_ARM_S3_SLOW16_ELEM2(186, 128, r2, r0, r1, r4, r5)
        GR_ARM_S3_SLOW16_ELEM2(190, 144, r2, r0, r1, r6, r7)
        GR_ARM_S3_SLOW16_ELEM2(194, 160, r2, r0, r1, r8, r9)
        GR_ARM_S3_SLOW16_ELEM2(198, 176, r2, r0, r1, r10, r11)
        GR_ARM_S3_COMMON_TAIL
    );
}

__attribute__((naked))
static void grTransformVertexShort3(Gerbera_s *state, _grVertex *vertex)
{
    (void)state;
    (void)vertex;
    __asm__ __volatile__(
        "ldr   r2, [r0, #1612]\n\t"
        "mov   r3, #0x80000000\n\t"
        "orr   r3, r3, #0x200000\n\t"
        "orr   r3, r3, #0x800\n\t"
        "orr   r3, r3, #2\n\t"
        "cmp   r2, r3\n\t"
        "beq   grTransformVertexShort3Identity_asm\n\t"
        GR_ARM_S3_COL_PREAMBLE
        GR_ARM_S3_GEN_ELEM(184, 120, r0, r4, r5, 20, 40)
        GR_ARM_S3_GEN_ELEM(188, 136, r0, r6, r7, 21, 41)
        GR_ARM_S3_GEN_ELEM(192, 152, r0, r8, r9, 22, 42)
        GR_ARM_S3_GEN_ELEM(196, 168, r0, r10, r11, 23, 43)
        GR_ARM_S3_GEN_ELEM(185, 124, r1, r4, r5, 24, 44)
        GR_ARM_S3_GEN_ELEM(189, 140, r1, r6, r7, 25, 45)
        GR_ARM_S3_GEN_ELEM(193, 156, r1, r8, r9, 26, 46)
        GR_ARM_S3_GEN_ELEM(197, 172, r1, r10, r11, 27, 47)
        "ldr   r2, [sp, #8]\n\t"
        GR_ARM_S3_GEN_ELEM(186, 128, r2, r4, r5, 28, 48)
        GR_ARM_S3_GEN_ELEM(190, 144, r2, r6, r7, 29, 49)
        GR_ARM_S3_GEN_ELEM(194, 160, r2, r8, r9, 30, 50)
        GR_ARM_S3_GEN_ELEM(198, 176, r2, r10, r11, 31, 51)
        GR_ARM_S3_COMMON_TAIL
        GR_ARM_S3_GEN_SLOW(r0, r2, r4, r5, 20, 40)
        GR_ARM_S3_GEN_SLOW(r0, r2, r6, r7, 21, 41)
        GR_ARM_S3_GEN_SLOW(r0, r2, r8, r9, 22, 42)
        GR_ARM_S3_GEN_SLOW(r0, r2, r10, r11, 23, 43)
        GR_ARM_S3_GEN_SLOW2(r1, r2, r0, r4, r5, 24, 44)
        GR_ARM_S3_GEN_SLOW2(r1, r2, r0, r6, r7, 25, 45)
        GR_ARM_S3_GEN_SLOW2(r1, r2, r0, r8, r9, 26, 46)
        GR_ARM_S3_GEN_SLOW2(r1, r2, r0, r10, r11, 27, 47)
        GR_ARM_S3_GEN_SLOW2(r2, r0, r1, r4, r5, 28, 48)
        GR_ARM_S3_GEN_SLOW2(r2, r0, r1, r6, r7, 29, 49)
        GR_ARM_S3_GEN_SLOW2(r2, r0, r1, r8, r9, 30, 50)
        GR_ARM_S3_GEN_SLOW2(r2, r0, r1, r10, r11, 31, 51)
    );
}
#else
static void grTransformVertexShort3Identity(Gerbera_s *state, _grVertex *vertex)
{
    _grVertexArray *array = &state->vertexArray[0];
    const short *src;
    int *position = (int *)&vertex->position;
    int x, y, z, w = 65536;
    unsigned int amax;
    int sh, e;

    GR_VP_CHECK(state && vertex, 230);
    GR_VP_CHECK(array->data, 231);
    GR_VP_CHECK(!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED), 232);
    src = (const short *)((const unsigned char *)array->data + array->stride * vertex->index);
    GR_VP_CHECK(grIsAligned(src, 2), 270);
    vertex->stateMask = (unsigned short)((vertex->stateMask & 0x3FFFu) |
                        (GR_POSITION_OBJECT_SPACE_FIXED << 14) | GR_VERTEXMASK_TRANSFORMED);
    x = (int)((unsigned int)src[0] << 16);
    y = (int)((unsigned int)src[1] << 16);
    z = (int)((unsigned int)src[2] << 16);
    position[0] = x;
    position[1] = y;
    position[2] = z;
    position[3] = 65536;

    amax = (unsigned int)(hgAbs32(x) | hgAbs32(y) | hgAbs32(z) | 0x10000);
    sh = hgClz32(amax) - 2;
    e = 140 - sh;
    if (sh >= 0)
    {
        x = (int)((unsigned int)x << sh);
        y = (int)((unsigned int)y << sh);
        z = (int)((unsigned int)z << sh);
        w = (int)((unsigned int)w << sh);
    }
    else
    {
        sh = -sh;
        x >>= sh; y >>= sh; z >>= sh; w >>= sh;
    }
    if (x == -1073741824 || y == -1073741824 ||
        z == -1073741824 || w == -1073741824)
    {
        x >>= 1; y >>= 1; z >>= 1; w >>= 1;
        ++e;
    }
    IVector4_set_noINF(&vertex->clipPosition, e, x, y, z, w);
    grComputeVertexClipCodes(state, vertex);
}

static void grTransformVertexShort3Fast16(Gerbera_s *state, _grVertex *vertex)
{
    _grVertexArray *array = &state->vertexArray[0];
    const short *src;
    int *position = (int *)&vertex->position;
    _IVertexTransform *pT = &state->objectToClipTransform;
    int s0, s1, s2;
    HGint64 acc0, acc1, acc2, acc3;
    HGuint64 amax;

    GR_VP_CHECK(state && vertex, 230);
    GR_VP_CHECK(array->data, 231);
    GR_VP_CHECK(!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED), 232);
    src = (const short *)((const unsigned char *)array->data + array->stride * vertex->index);
    GR_VP_CHECK(grIsAligned(src, 2), 270);
    vertex->stateMask = (unsigned short)((vertex->stateMask & 0x3FFFu) |
                        (GR_POSITION_OBJECT_SPACE_FIXED << 14) | GR_VERTEXMASK_TRANSFORMED);
    s0 = src[0];
    s1 = src[1];
    s2 = src[2];
    position[0] = (int)((unsigned int)s0 << 16);
    position[1] = (int)((unsigned int)s1 << 16);
    position[2] = (int)((unsigned int)s2 << 16);
    position[3] = 65536;

    acc0 = grVp_mulacc3ShRow(pT->w[0], pT->unnormalized[0], pT->sh[0], s0, s1, s2);
    acc1 = grVp_mulacc3ShRow(pT->w[1], pT->unnormalized[1], pT->sh[1], s0, s1, s2);
    acc2 = grVp_mulacc3ShRow(pT->w[2], pT->unnormalized[2], pT->sh[2], s0, s1, s2);
    acc3 = pT->w[3];
    if ((pT->cl & 0x3F000000u) != 0)
        acc3 = grVp_mulacc3ShRow(acc3, pT->unnormalized[3], pT->sh[3], s0, s1, s2);

    amax = (HGuint64)hgAbs64(acc0) | (HGuint64)hgAbs64(acc1) |
           (HGuint64)hgAbs64(acc2) | (HGuint64)hgAbs64(acc3);
    if (amax != 0)
    {
        int sh = hgClz64((HGint64)amax) - 1;
        int e = (int)pT->exp - sh + 1;
        int m0, m1, m2, m3;
        if (sh >= 32)
        {
            m0 = hgLsl64h(acc0, sh) >> 1;
            m1 = hgLsl64h(acc1, sh) >> 1;
            m2 = hgLsl64h(acc2, sh) >> 1;
            m3 = hgLsl64h(acc3, sh) >> 1;
        }
        else
        {
            m0 = hgLsl64h_0_31(acc0, sh) >> 1;
            m1 = hgLsl64h_0_31(acc1, sh) >> 1;
            m2 = hgLsl64h_0_31(acc2, sh) >> 1;
            m3 = hgLsl64h_0_31(acc3, sh) >> 1;
        }
        if (m0 == -1073741824 || m1 == -1073741824 ||
            m2 == -1073741824 || m3 == -1073741824)
        {
            m0 >>= 1; m1 >>= 1; m2 >>= 1; m3 >>= 1;
            ++e;
        }
        if (e > 0)
        {
            IVector4_set(&vertex->clipPosition, e, m0, m1, m2, m3);
            grComputeVertexClipCodes(state, vertex);
            return;
        }
    }

    IVector4_zero(&vertex->clipPosition);
    grComputeVertexClipCodes(state, vertex);
}

/* Three-component short positions have an implicit W of one. */
static void grTransformVertexShort3(Gerbera_s *state, _grVertex *vertex)
{
    _grVertexArray *array = &state->vertexArray[0];
    const short *src;
    int *position = (int *)&vertex->position;
    _IVertexTransform *pT = &state->objectToClipTransform;
    int s0, s1, s2;
    HGint64 acc0, acc1, acc2, acc3;
    HGuint64 amax;

    if (pT->cl == 0x80200802u)
    {
        grTransformVertexShort3Identity(state, vertex);
        return;
    }

    GR_VP_CHECK(state && vertex, 230);
    GR_VP_CHECK(array->data, 231);
    GR_VP_CHECK(!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED), 232);
    src = (const short *)((const unsigned char *)array->data + array->stride * vertex->index);
    GR_VP_CHECK(grIsAligned(src, 2), 270);
    vertex->stateMask = (unsigned short)((vertex->stateMask & 0x3FFFu) |
                        (GR_POSITION_OBJECT_SPACE_FIXED << 14) | GR_VERTEXMASK_TRANSFORMED);
    s0 = src[0];
    s1 = src[1];
    s2 = src[2];
    position[0] = (int)((unsigned int)s0 << 16);
    position[1] = (int)((unsigned int)s1 << 16);
    position[2] = (int)((unsigned int)s2 << 16);
    position[3] = 65536;

    acc0 = grVp_mulacc3ShRowGeneral(pT->w[0], pT->unnormalized[0], pT->sh[0], s0, s1, s2);
    acc1 = grVp_mulacc3ShRowGeneral(pT->w[1], pT->unnormalized[1], pT->sh[1], s0, s1, s2);
    acc2 = grVp_mulacc3ShRowGeneral(pT->w[2], pT->unnormalized[2], pT->sh[2], s0, s1, s2);
    acc3 = pT->w[3];
    if ((pT->cl & 0x3F000000u) != 0)
        acc3 = grVp_mulacc3ShRowGeneral(acc3, pT->unnormalized[3], pT->sh[3], s0, s1, s2);

    amax = (HGuint64)hgAbs64(acc0) | (HGuint64)hgAbs64(acc1) |
           (HGuint64)hgAbs64(acc2) | (HGuint64)hgAbs64(acc3);
    if (amax != 0)
    {
        int sh = hgClz64((HGint64)amax) - 1;
        int e = (int)pT->exp - sh + 1;
        int m0, m1, m2, m3;
        if (sh >= 32)
        {
            m0 = hgLsl64h(acc0, sh) >> 1;
            m1 = hgLsl64h(acc1, sh) >> 1;
            m2 = hgLsl64h(acc2, sh) >> 1;
            m3 = hgLsl64h(acc3, sh) >> 1;
        }
        else
        {
            m0 = hgLsl64h_0_31(acc0, sh) >> 1;
            m1 = hgLsl64h_0_31(acc1, sh) >> 1;
            m2 = hgLsl64h_0_31(acc2, sh) >> 1;
            m3 = hgLsl64h_0_31(acc3, sh) >> 1;
        }
        if (m0 == -1073741824 || m1 == -1073741824 ||
            m2 == -1073741824 || m3 == -1073741824)
        {
            m0 >>= 1; m1 >>= 1; m2 >>= 1; m3 >>= 1;
            ++e;
        }
        if (e > 0)
        {
            IVector4_set(&vertex->clipPosition, e, m0, m1, m2, m3);
            grComputeVertexClipCodes(state, vertex);
            return;
        }
    }

    IVector4_zero(&vertex->clipPosition);
    grComputeVertexClipCodes(state, vertex);
}
#endif

void (*grSelectFastVertexTransform(Gerbera_s *state))(Gerbera_s *state, _grVertex *vertex)
{
    if (grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) != 0)
        return NULL;
    if (state->vertexArray[0].type == GR_DATATYPE_FLOAT &&
        state->vertexArray[0].size == 3 &&
        state->renderStackAllocated &&
        !state->enabledUserClipPlaneIndices &&
        state->commonTransform.eligible &&
        state->primitiveType != GR_PRIMITIVETYPE_POINT &&
        grTestCapability(state, GR_CAPABILITY_LIGHTING) == 0)
        return grTransformVertexFloat3Common;
    if (state->vertexArray[0].type == GR_DATATYPE_INT16 &&
        state->vertexArray[0].size == 3)
    {
        const _IVertexTransform *pT = &state->objectToClipTransform;
        unsigned int rowBad = (((pT->sh[0] & 0xFFFFFFu) - 0x010101u) |
                               ((pT->sh[1] & 0xFFFFFFu) - 0x010101u) |
                               ((pT->sh[2] & 0xFFFFFFu) - 0x010101u));
        if (pT->cl == 0x80200802u)
            return grTransformVertexShort3Identity;
        if ((pT->cl & 0x3F000000u) != 0)
            rowBad |= ((pT->sh[3] & 0xFFFFFFu) - 0x010101u);
        if ((rowBad & 0xF0F0F0u) == 0)
            return grTransformVertexShort3Fast16;
#if !HG_ASSERT_ENABLED && (defined(__GNUC__) || defined(__clang__)) && \
    defined(__arm__) && !defined(__thumb__) && (__ARM_ARCH >= 5) && \
    (defined(__ARMEL__) || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
        if ((pT->cl & 0x3F000000u) != 0 &&
            (pT->sh[0] & pT->sh[1] & pT->sh[2] & pT->sh[3] & 0x101010u) == 0x101010u)
            return grTransformVertexShort3Slow16;
#endif
        return grTransformVertexShort3;
    }
    return NULL;
}

void grTransformVertexPreloaded(Gerbera_s *state, _grVertex *vertex, int weightsPreloaded)
{
    if (state->vertexArray[0].type == GR_DATATYPE_FLOAT &&
        state->vertexArray[0].size == 3 &&
        state->renderStackAllocated &&
        !state->enabledUserClipPlaneIndices &&
        state->commonTransform.eligible &&
        state->primitiveType != GR_PRIMITIVETYPE_POINT &&
        grTestCapability(state, GR_CAPABILITY_LIGHTING) == 0 &&
        grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) == 0)
        grTransformVertexFloat3Common(state, vertex);
    else if (state->vertexArray[0].type == GR_DATATYPE_INT16 &&
        state->vertexArray[0].size == 3 &&
        grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) == 0)
        grTransformVertexShort3(state, vertex);
    else
        grTransformVertexGeneral(state, vertex, weightsPreloaded);
}

/* Compute the directional terms without changing the light's position,
 * attenuation or halfway vector. Keep ld/ls as observable scratch output. */
static inline void grDirectionalLightTerms(_grLight *l, int shininess, int *eyeNormal, int nSides)
{
    unsigned int mask = (unsigned int)l->lightMask;
    int ndotL = 0, specDot = 0;

    if ((mask & 0xC) != 0)
    {
        int signDiffuse;
        ndotL = XNormal3_dot(l->nVP, eyeNormal);
        signDiffuse = XScalar_signBit(ndotL);
        if ((signDiffuse == 0 || nSides != 0) && (mask & 8) != 0)
        {
            int halfDot = XNormal3_dot(l->halfway, eyeNormal);
            if (XScalar_signBit(halfDot) == signDiffuse)
            {
                if (XScalar_zcmpne(shininess) != 0)
                {
                    specDot = XScalar_abs(halfDot);
                    if (XScalar_cmpne(shininess, 0x200000) != 0)
                        specDot = XScalar_pow(specDot, shininess);
                }
                else
                    specDot = 0x200000;
            }
        }
    }
    l->ld = ndotL;
    l->ls = specDot;
}

/* --- light (decompile 28554-28693; assert lines 774/781/799/807) -------------
 * Per-vertex per-light lighting: computes L (positional), the spot factor,
 * the attenuation and the diffuse/specular dots, storing the results into
 * the light record's per-vertex scratch (la/ld/ls) for the
 * color accumulation in grLightVertex. Mutates nVP for positional lights
 * (it doubles as the per-vertex L vector) and halfway (specular). */

void light(_grLight **lights, int shininess, _IVector3 *pos, int *eyeNormal, int nSides, int bDirectional)
{
    if (bDirectional != 0)
    {
        for (; *lights != NULL; ++lights)
            grDirectionalLightTerms(*lights, shininess, eyeNormal, nSides);
        return;
    }
    for (; *lights != NULL; ++lights)
    {
        _grLight *l = *lights;
        unsigned int mask = (unsigned int)l->lightMask;
        int atten = 0x200000;
        int spot = 0;
        int ndotL = 0;
        int specDot = 0;
        int lvec;
        int signDiffuse;

        l->la = 0;
        if ((mask & 0xE) == 0)
            continue;
        lvec = IVector3_normsub(l->nVP, ( _IVector3 *)&l->positionXYZ, pos);
        if ((mask & 0x10) != 0)
        {
            if ((l->cosSpotCutoff < 0) && grAssert("light->cosSpotCutoff >= 0", "grVertexPipeline.inl", 774) != 0)
                grDie(0);
            spot = XScalar_min(XNormal3_dot(l->nVP, l->spotDirection), 0x200000);
            if (XScalar_cmplt(spot, l->cosSpotCutoff) == 0)
            {
                /* inside the spot cone */
                {
                    int ok = 1;
                    int cond = (XScalar_zcmpge(spot) != 0) && (XScalar_cmple(spot, 0x200000) != 0);

                    if (!cond && grAssert("XScalar_zcmpge(spot) && XScalar_cmple(spot,IM_XSCALAR_ONE)",
                                          "grVertexPipeline.inl", 781) != 0)
                        ok = 0;
                    if (ok == 0)
                        grDie(0);
                }
                goto attenStage;
            }
            /* outside the cone: no contribution at all */
            continue;
        }
    attenStage:
        if ((mask & 0x20) == 0 ||
            (atten = IScalar_attenuateRcp(lvec, &l->attenuation), XScalar_zcmpeq(atten) == 0))
        {
            if ((mask & 0x10) != 0)
            {
                if (XScalar_cmpne(l->spotExponent, 0x200000) != 0)
                    spot = XScalar_pow(spot, l->spotExponent);
                {
                    int ok = 1;
                    int cond = (XScalar_zcmpge(spot) != 0) && (XScalar_cmple(spot, 0x200000) != 0);

                    if (!cond && grAssert("XScalar_zcmpge(spot) && XScalar_cmple(spot,IM_XSCALAR_ONE)",
                                          "grVertexPipeline.inl", 799) != 0)
                        ok = 0;
                    if (ok == 0)
                        grDie(0);
                }
                atten = XScalar_mul(spot, atten);
                if (XScalar_zcmpeq(atten) != 0)
                    continue;   /* zero total attenuation: no contribution */
            }
            {
                if ((atten < 0) && grAssert("xAtten >= 0", "grVertexPipeline.inl", 807) != 0)
                    grDie(0);
                l->la = atten;
            }
            if ((mask & 0xC) != 0)
            {
                ndotL = XNormal3_dot(l->nVP, eyeNormal);
                signDiffuse = XScalar_signBit(ndotL);
                if (signDiffuse == 0 || nSides != 0)
                {
                    if ((mask & 4) != 0)
                        ndotL = XScalar_mul_sat(atten, ndotL);
                    if ((mask & 8) != 0)
                    {
                        /* halfway = normalize(L + view(0,0,1)): the view
                         * offset is folded into nVP[2] itself */
                        l->nVP[2] = XScalar_add(l->nVP[2], 0x200000);
                        XNormal3_normalize(l->halfway, l->nVP);
                        {
                            int halfDot = XNormal3_dot(l->halfway, eyeNormal);

                            if (XScalar_signBit(halfDot) == signDiffuse)
                            {
                                if (XScalar_zcmpne(shininess) != 0)
                                {
                                    specDot = XScalar_abs(halfDot);
                                    if (XScalar_cmpne(shininess, 0x200000) != 0)
                                        specDot = XScalar_pow(specDot, shininess);
                                    specDot = XScalar_mulu_sat(atten, specDot);
                                }
                                else
                                {
                                    specDot = atten;
                                }
                            }
                        }
                    }
                }
            }
            l->ld = ndotL;
            l->ls = specDot;
        }
    }
}

/* --- grLightVertex (decompile 28696-29135; asserts grVertexPipeline.inl
 * 887/956/960; the __assert(file,line,expr) sites keep their strings) -------
 * Transforms the normal to eye space (palette-weighted when skinning),
 * runs light() over the directional and positional lists and accumulates
 * the lit color into the vertex record's u16 color rows. */

void grLightVertex(Gerbera_s *state, _grVertex *v)
{
    int nSides;
    int eyeNormal[3];
    int normals[4][3];
    int shininess;
    HGint64 accFront[3];          /* 3x64-bit accumulators (r, g, b) */
    HGint64 accFrontSpec[3];
    HGint64 accBack[3];
    HGint64 accBackSpec[3];
    HGint64 tmpAcc[3];
    _XColor3 cmColor;
    _grLight **iter;
    _grLight *l;
    int i;

    if (!(state != NULL && v != NULL) && grAssert("state && v", "grVertexPipeline.inl", 887) != 0)
        grDie(0);
    nSides = (state->lightingSideMask & 2) != 0;
    XNormal3_copy(eyeNormal, v->normal);
    if (grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) != 0 &&
        grTestCapability(state, GR_CAPABILITY_MATRIX_INDEX_ARRAY) != 0 &&
        grTestCapability(state, GR_CAPABILITY_WEIGHT_ARRAY) != 0 &&
        state->vertexArray[3].data != NULL &&
        state->vertexArray[4].data != NULL)
    {
        unsigned int indices = v->weightIndices;
        int count = hgMin32(state->vertexArray[3].size, state->vertexArray[4].size);

        count = hgMin32(count, 4);
        for (i = 0; i < count; ++i)
        {
            XNormalTransform_mulNormal(&normals[i][0], &state->normalsToEyePal[indices & 0xFF], eyeNormal, 0);
            indices >>= 8;
        }
        XNormal3_accfv(eyeNormal, normals, v->weights, count);
        XNormal3_normalize(eyeNormal, eyeNormal);
    }
    else
    {
        int normalize = grTestCapability(state, GR_CAPABILITY_NORMALIZE);

        XNormalTransform_mulNormal(eyeNormal, &state->normalsToEyeTransform, eyeNormal, normalize);
    }
    shininess = state->material.shininess;
    /* One-sided, directional-only lighting can consume the terms in
     * light-list order as they are computed. Keep the same accumulation
     * and vertex stores, including the aliased final back-color fields. */
    if (nSides == 0 && state->l_positional[0] == NULL &&
        grTestCapability(state, GR_CAPABILITY_COLOR_MATERIAL) == 0 &&
        grGetVertexPositionState(v) != GR_POSITION_EYE_SPACE_CARTESIAN)
    {
        XColor3Acc_set(accFront, &state->baseColor);
        for (iter = state->l_directional; *iter != NULL; ++iter)
        {
            l = *iter;
            grDirectionalLightTerms(l, shininess, eyeNormal, 0);
            if (XScalar_zcmplt(l->ld) == 0)
            {
                if (XScalar_zcmpgt(l->ld) != 0)
                    XColor3Acc_madd(accFront, accFront, &l->premul_colors[1], l->ld);
                if (XScalar_zcmpgt(l->ls) != 0)
                    XColor3Acc_madd(accFront, accFront, &l->premul_colors[2], l->ls);
            }
        }
        v->color.final[0][3] = (unsigned short)(state->material.alpha & 0xFF00u);
        XColor3Acc_toRGB(&v->color.final[0][0], accFront);
        v->color.input.rgb[0].b = v->color.input.rgb[0].r;
        v->color.final[1][2] = v->color.final[0][2];
        return;
    }
    light(state->l_directional, shininess, NULL, eyeNormal, nSides, 1);
    if (grGetVertexPositionState(v) == GR_POSITION_EYE_SPACE_CARTESIAN)
    {
        if (state->l_positional[0] == NULL)
            /* original: __assert("grVertexPipeline.inl", 956, "state->l_positional[0]") */
            imAssert("state->l_positional[0]", "grVertexPipeline.inl", 956);
    }
    else if (state->l_positional[0] != NULL)
    {
        /* original: __assert("grVertexPipeline.inl", 960, "!state->l_positional[0]") */
        imAssert("!state->l_positional[0]", "grVertexPipeline.inl", 960);
    }
    light(state->l_positional, shininess, (_IVector3 *)&v->position, eyeNormal, nSides, 0);
    if (grTestCapability(state, GR_CAPABILITY_COLOR_MATERIAL) != 0)
    {
        /* CM path: ambient + diffuse terms accumulate raw light colors and
         * get scaled by the per-vertex color material color at the end;
         * specular uses the material-premultiplied slot. */
        XColor3Acc_set(accFront, &state->ambientLightSum);
        XColor3Acc_setZero(accFrontSpec);
        cmColor = v->color.input.rgb[0];
        v->color.final[0][3] = (unsigned short)(v->color.input.alpha & 0xFF00u);
        if (nSides != 0)
        {
            XColor3Acc_set(accBack, &state->ambientLightSum);
            XColor3Acc_setZero(accBackSpec);
            for (iter = state->l_directional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpgt(l->ld) != 0)
                {
                    XColor3Acc_madd(accFront, accFront, &l->colorsx[1], l->ld);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accFrontSpec, accFrontSpec, &l->colorsx[2], l->ls);
                }
                else if (XScalar_zcmplt(l->ld) != 0)
                {
                    int adot = XScalar_abs(l->ld);

                    XColor3Acc_madd(accBack, accBack, &l->colorsx[1], adot);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accBackSpec, accBackSpec, &l->colorsx[2], l->ls);
                }
            }
            for (iter = state->l_positional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpeq(l->la) == 0)
                {
                    XColor3Acc_muls(tmpAcc, &l->premul_colors[0], l->la);
                    XColor3Acc_addAcc(accFront, accFront, tmpAcc);
                    XColor3Acc_addAcc(accBack, accBack, tmpAcc);
                    if (XScalar_zcmpgt(l->ld) != 0)
                    {
                        XColor3Acc_madd(accFront, accFront, &l->colorsx[1], l->ld);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accFrontSpec, accFrontSpec, &l->premul_colors[2], l->ls);
                    }
                    else if (XScalar_zcmplt(l->ld) != 0)
                    {
                        int adot = XScalar_abs(l->ld);

                        XColor3Acc_madd(accBack, accBack, &l->colorsx[1], adot);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accBackSpec, accBackSpec, &l->premul_colors[2], l->ls);
                    }
                }
            }
            XColor3Acc_scaleColor(accFront, accFront, &cmColor);
            XColor3Acc_addColor(accFront, accFront, &state->material.colors[3]);
            XColor3Acc_addAcc(accFront, accFront, accFrontSpec);
            XColor3Acc_toRGB(&v->color.final[0][0], accFront);
            XColor3Acc_scaleColor(accBack, accBack, &cmColor);
            XColor3Acc_addColor(accBack, accBack, &state->material.colors[3]);
            XColor3Acc_addAcc(accBack, accBack, accBackSpec);
            XColor3Acc_toRGB(&v->color.final[1][0], accBack);
        }
        else
        {
            for (iter = state->l_directional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmplt(l->ld) == 0)
                {
                    if (XScalar_zcmpgt(l->ld) != 0)
                        XColor3Acc_madd(accFront, accFront, &l->colorsx[1], l->ld);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accFrontSpec, accFrontSpec, &l->premul_colors[2], l->ls);
                }
            }
            for (iter = state->l_positional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpeq(l->la) == 0)
                {
                    XColor3Acc_madd(accFront, accFront, &l->colorsx[0], l->la);
                    if (XScalar_zcmplt(l->ld) == 0)
                    {
                        if (XScalar_zcmpgt(l->ld) != 0)
                            XColor3Acc_madd(accFront, accFront, &l->colorsx[1], l->ld);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accFrontSpec, accFrontSpec, &l->premul_colors[2], l->ls);
                    }
                }
            }
            XColor3Acc_scaleColor(accFront, accFront, &cmColor);
            XColor3Acc_addColor(accFront, accFront, &state->material.colors[3]);
            XColor3Acc_addAcc(accFront, accFront, accFrontSpec);
            XColor3Acc_toRGB(&v->color.final[0][0], accFront);
            v->color.input.rgb[0].b = v->color.input.rgb[0].r;
            v->color.final[1][2] = v->color.final[0][2];
        }
    }
    else
    {
        /* base color path: everything accumulates material-premultiplied
         * light colors on top of baseColor (emission + global ambient). */
        XColor3Acc_set(accFront, &state->baseColor);
        v->color.final[0][3] = (unsigned short)(state->material.alpha & 0xFF00u);
        if (nSides != 0)
        {
            XColor3Acc_set(accBack, &state->baseColor);
            for (iter = state->l_directional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpgt(l->ld) != 0)
                {
                    XColor3Acc_madd(accFront, accFront, &l->premul_colors[1], l->ld);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accFront, accFront, &l->premul_colors[2], l->ls);
                }
                else if (XScalar_zcmplt(l->ld) != 0)
                {
                    int adot = XScalar_abs(l->ld);

                    XColor3Acc_madd(accBack, accBack, &l->premul_colors[1], adot);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accBack, accBack, &l->premul_colors[2], l->ls);
                }
            }
            for (iter = state->l_positional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpeq(l->la) == 0)
                {
                    XColor3Acc_muls(tmpAcc, &l->premul_colors[0], l->la);
                    XColor3Acc_addAcc(accFront, accFront, tmpAcc);
                    XColor3Acc_addAcc(accBack, accBack, tmpAcc);
                    if (XScalar_zcmpgt(l->ld) != 0)
                    {
                        XColor3Acc_madd(accFront, accFront, &l->premul_colors[1], l->ld);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accFront, accFront, &l->premul_colors[2], l->ls);
                    }
                    else if (XScalar_zcmplt(l->ld) != 0)
                    {
                        int adot = XScalar_abs(l->ld);

                        XColor3Acc_madd(accBack, accBack, &l->premul_colors[1], adot);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accBack, accBack, &l->premul_colors[2], l->ls);
                    }
                }
            }
            XColor3Acc_toRGB(&v->color.final[0][0], accFront);
            XColor3Acc_toRGB(&v->color.final[1][0], accBack);
        }
        else
        {
            for (iter = state->l_directional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmplt(l->ld) == 0)
                {
                    if (XScalar_zcmpgt(l->ld) != 0)
                        XColor3Acc_madd(accFront, accFront, &l->premul_colors[1], l->ld);
                    if (XScalar_zcmpgt(l->ls) != 0)
                        XColor3Acc_madd(accFront, accFront, &l->premul_colors[2], l->ls);
                }
            }
            for (iter = state->l_positional; *iter != NULL; ++iter)
            {
                l = *iter;
                if (XScalar_zcmpeq(l->la) == 0)
                {
                    XColor3Acc_madd(accFront, accFront, &l->premul_colors[0], l->la);
                    if (XScalar_zcmplt(l->ld) == 0)
                    {
                        if (XScalar_zcmpgt(l->ld) != 0)
                            XColor3Acc_madd(accFront, accFront, &l->premul_colors[1], l->ld);
                        if (XScalar_zcmpgt(l->ls) != 0)
                            XColor3Acc_madd(accFront, accFront, &l->premul_colors[2], l->ls);
                    }
                }
            }
            XColor3Acc_toRGB(&v->color.final[0][0], accFront);
            v->color.input.rgb[0].b = v->color.input.rgb[0].r;
            v->color.final[1][2] = v->color.final[0][2];
        }
    }
}

/* --- grApplyLighting (decompile 29139-29223; asserts grVertexPipeline.inl
 * 1275/1286/1311) ------------------------------------------------------------
 * Brings every listed vertex to eye space (state >= 2; cartesian state 3
 * stores the fog Z in the position's w slot) and lights it. With no
 * positional lights (transformVE == 0) the object-space position suffices. */

void grApplyLighting(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *lista;

    if (state->transformVE != 0)
    {
        GR_FETCH_CHECK(vertexList, 1275);
        vertexBuffer = GR_VERTEX_BUFFER(state);
        lista = vertexList + 1;
        vertex = &vertexBuffer[*vertexList];
        do
        {
            if (grGetVertexPositionState(vertex) < GR_POSITION_EYE_SPACE)
            {
                if (grGetVertexPositionState(vertex) != GR_POSITION_OBJECT_SPACE_FLOAT)
                {
                    if (grGetVertexPositionState(vertex) != GR_POSITION_OBJECT_SPACE_FIXED)
                        imAssert("grGetVertexPositionState( v ) == GR_POSITION_OBJECT_SPACE_FIXED",
                                 "grVertexPipeline.inl", 1286);
                    IVertexTransform_mulxv4s(vertex->position.objectSpaceFloat,
                                             &state->modelviewTransform,
                                             (int *)&vertex->position);
                }
                else
                {
                    IVertexTransform_mulfv4s(vertex->position.objectSpaceFloat,
                                             &state->modelviewTransform,
                                             vertex->position.objectSpaceFloat);
                }
                grSetVertexPositionState(vertex, GR_POSITION_EYE_SPACE);
            }
            if (grGetVertexPositionState(vertex) == GR_POSITION_EYE_SPACE)
            {
                _IFVector4 tmp;

                grSetVertexPositionState(vertex, GR_POSITION_EYE_SPACE_CARTESIAN);
                IFVector4_copy(&tmp, vertex->position.objectSpaceFloat);
                vertex->position.objectSpaceFloat[0].m[3] = IFVector4_getf(&tmp, 2);
                IFVector4_tov3((_IVector3 *)&vertex->position, &tmp);
            }
            grLightVertex(state, vertex);
            vertex = &vertexBuffer[*lista];
        } while (*lista++ != 0xFF);
    }
    else
    {
        GR_FETCH_CHECK(vertexList, 1311);
        vertexBuffer = GR_VERTEX_BUFFER(state);
        lista = vertexList + 1;
        vertex = &vertexBuffer[*vertexList];
        do
        {
            grLightVertex(state, vertex);
            vertex = &vertexBuffer[*lista];
        } while (*lista++ != 0xFF);
    }
}

/* --- grViewportTransformVertex (decompile 28120-28131) ---------------------- */

void grViewportTransformVertex(Gerbera_s *state, _grVertex *vertex)
{
    vertex->stateMask |= GR_VERTEXMASK_VIEWPORT_TRANSFORMED;
    IVector4_viewportTransform(&vertex->screenPosition[0],
                               &vertex->clipPosition,
                               state->viewportScale.normal,
                               state->viewportOffset.normal,
                               8);
}

/* --- grSetupCulling (decompile 31562-31699; asserts grApiFast.inl
 * 108/113/157) ----------------------------------------------------------------
 * Validates the primitive type, stores it, fast-rejects draws that cannot
 * produce fragments (no vertex data / always-false alpha test / empty
 * scissor / fully culled faces) and refreshes every derived transform:
 * objectToClip = projection*modelview, the modelview/projection
 * _IVertexTransforms, the per-plane clip matrices (projection * userClip-
 * Matrix[p], inverted) applied to the original plane equations, and the
 * palette matrix transforms. Returns GR_COMMAND_STOP or GR_COMMAND_RENDER. */

int grSetupCulling(Gerbera_s *state, int primitiveType)
{
    int reject;
    int isTriangleLinePoint;
    int isValidType;
    int plane;
    int packedIndices;
    int sign;
    int pal;
    _IMatrix4x4 tmp;
    _IMatrix4x4 tmpPlane;
    _IMatrix4x4 tmpInv;

    if ((state == NULL) && grAssert("state", "grApiFast.inl", 108) != 0)
        grDie(0);
    isTriangleLinePoint = (primitiveType == GR_PRIMITIVETYPE_TRIANGLE ||
                           primitiveType == GR_PRIMITIVETYPE_LINE ||
                           primitiveType == GR_PRIMITIVETYPE_POINT);
    isValidType = isTriangleLinePoint || primitiveType == GR_PRIMITIVETYPE_TEXTURE_RECT;
    if (!isValidType)
    {
        int die__ = 1;

        if (grAssert("primitiveType == GR_PRIMITIVETYPE_TRIANGLE || primitiveType == GR_PRIMITIVETYPE_LINE || primitiveType == GR_PRI"
                     "MITIVETYPE_POINT || primitiveType == GR_PRIMITIVETYPE_TEXTURE_RECT",
                     "grApiFast.inl", 113) == 0)
            die__ = 0;
        if (die__ != 0)
            grDie(0);
    }
    state->primitiveType = (unsigned char)primitiveType;
    if (primitiveType != GR_PRIMITIVETYPE_TEXTURE_RECT)
    {
        int hasVertexData = 1;

        if (grTestCapability(state, GR_CAPABILITY_VERTEX_ARRAY) != 0)
            hasVertexData = (state->vertexArray[0].data == NULL);
        if (hasVertexData)
            return GR_COMMAND_STOP;
    }
    reject = 0;
    if (grTestCapability(state, GR_CAPABILITY_ALPHA_TEST) != 0 && state->alphaFunc == 0)
        reject = 1;
    if (grIsRectangleEmpty(&state->renderScissor) != 0)
        reject = 1;
    if (state->rejectAllStateDirty == 0)
    {
        int ok = 1;

        if (state->rejectAllState != reject &&
            grAssert("state->rejectAllState == reject", "grApiFast.inl", 157) != 0)
            ok = 0;
        if (ok == 0)
            grDie(0);
    }
    state->rejectAllState = reject;
    state->rejectAllStateDirty = 0;
    if (state->rejectAllState != 0)
        return GR_COMMAND_STOP;
    if (primitiveType == GR_PRIMITIVETYPE_TRIANGLE &&
        grTestCapability(state, GR_CAPABILITY_CULL_FACE) != 0 &&
        state->cullMask == 3)
    {
        return GR_COMMAND_STOP;
    }
    if (grIsMatrixDirty(state, GR_MATRIXMODE_MODELVIEW) != 0 ||
        grIsMatrixDirty(state, GR_MATRIXMODE_PROJECTION) != 0)
    {
        IMatrix4x4_mul(&tmp, &state->matrix[GR_MATRIXMODE_PROJECTION],
                       &state->matrix[GR_MATRIXMODE_MODELVIEW]);
        IVertexTransform_copy(&state->objectToClipTransform, &tmp);
        IVertexTransform_prepareCommon(&state->commonTransform, &state->objectToClipTransform);
        if (grIsMatrixDirty(state, GR_MATRIXMODE_MODELVIEW) != 0)
        {
            IVertexTransform_copy(&state->modelviewTransform,
                                  &state->matrix[GR_MATRIXMODE_MODELVIEW]);
            state->normalsToEyeDirty = 1;
        }
        if (grIsMatrixDirty(state, GR_MATRIXMODE_PROJECTION) != 0)
            IVertexTransform_copy(&state->projectionTransform,
                                  &state->matrix[GR_MATRIXMODE_PROJECTION]);
        grSetMatrixDirty(state, GR_MATRIXMODE_MODELVIEW, 0);
        grSetMatrixDirty(state, GR_MATRIXMODE_PROJECTION, 0);
    }
    packedIndices = 0;
    for (plane = 5; plane >= 0; --plane)
    {
        if (grTestCapability(state, GR_CAPABILITY_CLIP_PLANE0 + plane) != 0)
        {
            packedIndices = plane + 1 + 16 * packedIndices;
            IMatrix4x4_mul(&tmpPlane, &state->matrix[GR_MATRIXMODE_PROJECTION],
                           &state->userClipMatrix[plane]);
            IMatrix4x4_inverse(&tmpInv, &tmpPlane, 1);
            sign = IFVector4_getFirstNonZeroSign(&state->origUserClipPlanes[plane]);
            if (sign >= 0)
                IFVector4_copy(&state->userClipPlanes[plane],
                               &state->origUserClipPlanes[plane]);
            else
                IFVector4_negate(&state->userClipPlanes[plane],
                                 &state->origUserClipPlanes[plane]);
            IMatrix4x4_mulfv4(&state->userClipPlanes[plane], &tmpInv,
                              &state->userClipPlanes[plane]);
            if (sign < 0)
                IFVector4_negate(&state->userClipPlanes[plane],
                                 &state->userClipPlanes[plane]);
        }
    }
    state->enabledUserClipPlaneIndices = packedIndices;
    if (grTestCapability(state, GR_CAPABILITY_MATRIX_PALETTE) != 0)
    {
        for (pal = 8; pal >= 0; --pal)
        {
            if (grIsMatrixDirty(state, pal + 4) != 0)
            {
                IVertexTransform_copy(&state->matrixPalTransform[pal],
                                      &state->matrix[pal + 4]);
                state->normalsToEyePalDirty[pal] = 1;
                grSetMatrixDirty(state, pal + 4, 0);
            }
        }
    }
    /* Plans belong to this draw's object-to-clip transform. */
    state->floatTransformRecent = 0;
    state->floatTransformMisses = 0;
    state->floatTransformPlan[0].inputExponent = ~0u;
    state->floatTransformPlan[1].inputExponent = ~0u;
    return GR_COMMAND_RENDER;
}
