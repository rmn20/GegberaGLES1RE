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

int grGetFogZ(Gerbera_s *state, _grVertex *vertex)
{
    int result;

    switch (grGetVertexPositionState(vertex))
    {
        case 0:
            result = IVertexTransform_zmulfv4(&state->modelviewTransform,
                                              vertex->position.objectSpaceFloat);
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

void grApplyFogLINEAR(Gerbera_s *state, unsigned char *vertexList)
{
    _grVertex *vertexBuffer;
    _grVertex *vertex;
    unsigned char *vertexLista;
    int fogOORange;
    int d;

    GR_FETCH_CHECK(vertexList, 1925);
    vertexBuffer = GR_VERTEX_BUFFER(state);
    vertexLista = vertexList + 1;
    vertex = &vertexBuffer[*vertexList];
    do
    {
        d = IScalar_abs(grGetFogZ(state, vertex));
        fogOORange = state->fogOORange;
        vertex->color.final[1][3] =
            (unsigned short)~(unsigned short)hgClamp32_0_x(
                IScalar_scalarToFixed(IScalar_mul(IScalar_sub(state->fogEnd, d), fogOORange), 16),
                0xFFFF);
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

unsigned int grComputeVertexClipCodes(Gerbera_s *state, _grVertex *vertex)
{
    unsigned int clipMask;
    unsigned int i;

    clipMask = IVector4_clip(&vertex->clipPosition, 2);
    if (state->enabledUserClipPlaneIndices != 0)
    {
        /* enabledUserClipPlaneIndices packs (plane+1) per enabled plane as
         * base-16 digits (grSetupCulling, planes walked 5..0); the consumer
         * reads plane digit-1 (EPOC: base+4728+20*digit-20) and ORs the
         * outside test into bit digit+9. The WINSCW decompile reaches the
         * plane equations through the normalsToEyePal alias - see grState.h. */
        for (i = (unsigned int)state->enabledUserClipPlaneIndices; i != 0; i >>= 4)
            clipMask |= (unsigned int)grIsOutsideUserClipPlane(
                            &state->userClipPlanes[(i & 0xF) - 1],
                            &vertex->clipPosition)
                        << ((i & 0xF) + 9);
    }
    vertex->clipMask = clipMask;
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

/* Three-component short positions have an implicit W of one. */
#if defined(__GNUC__) && !defined(__clang__) && defined(__arm__) && __ARM_ARCH >= 6
__attribute__((optimize("no-schedule-insns")))
#endif
static void grTransformVertexShort3(Gerbera_s *state, _grVertex *vertex)
{
    _grVertexArray *array = &state->vertexArray[0];
    const short *src;
    int *position = (int *)&vertex->position;

    GR_VP_CHECK(state && vertex, 230);
    GR_VP_CHECK(array->data, 231);
    GR_VP_CHECK(!(vertex->stateMask & GR_VERTEXMASK_TRANSFORMED), 232);
    src = (const short *)((const unsigned char *)array->data + array->stride * vertex->index);
    GR_VP_CHECK(grIsAligned(src, 2), 270);
    vertex->stateMask |= GR_VERTEXMASK_TRANSFORMED;
    position[0] = (int)src[0] * 65536;
    position[1] = (int)src[1] * 65536;
    position[2] = (int)src[2] * 65536;
    position[3] = 65536;
    grSetVertexPositionState(vertex, GR_POSITION_OBJECT_SPACE_FIXED);
    IVertexTransform_mulxv4(&vertex->clipPosition, &state->objectToClipTransform, position);
    grComputeVertexClipCodes(state, vertex);
}

void grTransformVertexPreloaded(Gerbera_s *state, _grVertex *vertex, int weightsPreloaded)
{
    if (state->vertexArray[0].type == GR_DATATYPE_INT16 &&
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
