/* eglBase.c - Gerbera EGL 1.1 display, config, surface, swap, render-to-texture,
 * query, setjmp, and eglGetProcAddress entry points (ported from eglBase.inl).
 */
#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "gerbera/egl/eglInternal.h"
#include "hg/hgInt32.h"
#include "hg/hgMemory.h"

/* --- Internal state & object lookup helpers -------------------------------- */

int eglIsValidState(eglState_s *state)
{
    if (state != NULL && state->windowDisplay != NULL)
    {
        eglContext_s *ctx;
        for (ctx = state->windowDisplay->contextList; ctx != NULL; ctx = ctx->next)
            oglCheckValidity(ctx->esContext);
    }
    return 1;
}

eglDisplay_s *eglMapDisplay(EGLDisplay dpy)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglBase.inl", 140, "state", state != NULL);
    if (dpy == 1)
        return state->windowDisplay;
    return NULL;
}

eglSurface_s *eglMapSurface(eglDisplay_s *display, EGLSurface id)
{
    eglSurface_s *s;

    if (display == NULL)
        return NULL;

    for (s = display->surfaceList; s != NULL; s = s->next)
    {
        /* The original keeps referenced, destroyed objects addressable until
         * release unlinks them, including through repeated destroy calls. */
        if (s->id == id)
            return s;
    }
    return NULL;
}

eglContext_s *eglMapContext(eglDisplay_s *display, EGLContext id)
{
    eglContext_s *c;

    if (display == NULL)
        return NULL;

    for (c = display->contextList; c != NULL; c = c->next)
    {
        if (c->id == id)
            return c;
    }
    return NULL;
}

int eglDisplayOwnsSurface(eglDisplay_s *display, eglSurface_s *surface)
{
    eglSurface_s *s;

    for (s = display->surfaceList; s != NULL; s = s->next)
    {
        if (s == surface)
            return 1;
    }
    return 0;
}

int eglDisplayOwnsContext(eglDisplay_s *display, eglContext_s *context)
{
    eglContext_s *c;

    for (c = display->contextList; c != NULL; c = c->next)
    {
        if (c == context)
            return 1;
    }
    return 0;
}

void eglDisplayAddContext(eglDisplay_s *display, eglContext_s *context)
{
    EGL_ASSERT_AT("eglBase.inl", 205, "display && context",
                  display != NULL && context != NULL);
    EGL_ASSERT_AT("eglBase.inl", 206, "!eglDisplayOwnsContext(display, context)",
                  eglDisplayOwnsContext(display, context) == 0);

    context->display = display;
    context->id = ++display->contextCount;
    context->prev = NULL;
    context->next = display->contextList;
    if (display->contextList != NULL)
        display->contextList->prev = context;
    display->contextList = context;
}

void eglSurfaceReleaseRef(eglSurface_s *surface)
{
    if (surface != NULL && eglReleaseRef(&surface->refCount) != 0)
        eglFreeSurface(surface);
}

void eglContextReleaseRef(eglContext_s *context)
{
    if (context != NULL && eglReleaseRef(&context->refCount) != 0)
        eglFreeContext(context);
}

int eglGetConfigAttributeOffset(int attribute)
{
    switch (attribute)
    {
    case EGL_BUFFER_SIZE:             return (int)offsetof(eglConfig, colorBits);
    case EGL_ALPHA_SIZE:              return (int)offsetof(eglConfig, alphaBits);
    case EGL_BLUE_SIZE:               return (int)offsetof(eglConfig, blueBits);
    case EGL_GREEN_SIZE:              return (int)offsetof(eglConfig, greenBits);
    case EGL_RED_SIZE:                return (int)offsetof(eglConfig, redBits);
    case EGL_DEPTH_SIZE:              return (int)offsetof(eglConfig, depthBits);
    case EGL_STENCIL_SIZE:            return (int)offsetof(eglConfig, stencilBits);
    case EGL_CONFIG_CAVEAT:           return (int)offsetof(eglConfig, configCaveat);
    case EGL_CONFIG_ID:               return (int)offsetof(eglConfig, configID);
    case EGL_LEVEL:                   return (int)offsetof(eglConfig, level);
    case EGL_NATIVE_RENDERABLE:       return (int)offsetof(eglConfig, nativeRenderable);
    case EGL_NATIVE_VISUAL_ID:        return (int)offsetof(eglConfig, nativeVisualID);
    case EGL_NATIVE_VISUAL_TYPE:      return (int)offsetof(eglConfig, nativeVisualType);
    case EGL_SAMPLES:                 return (int)offsetof(eglConfig, numSamples);
    case EGL_SAMPLE_BUFFERS:          return (int)offsetof(eglConfig, numSampleBuffers);
    case EGL_SURFACE_TYPE:            return (int)offsetof(eglConfig, surfaceType);
    case EGL_TRANSPARENT_TYPE:        return (int)offsetof(eglConfig, transparentType);
    case EGL_TRANSPARENT_BLUE_VALUE:  return (int)offsetof(eglConfig, transparentBlueValue);
    case EGL_TRANSPARENT_GREEN_VALUE: return (int)offsetof(eglConfig, transparentGreenValue);
    case EGL_TRANSPARENT_RED_VALUE:   return (int)offsetof(eglConfig, transparentRedValue);
    case EGL_BIND_TO_TEXTURE_RGB:     return (int)offsetof(eglConfig, bindToTextureRgb);
    case EGL_BIND_TO_TEXTURE_RGBA:    return (int)offsetof(eglConfig, bindToTextureRgba);
    case EGL_MIN_SWAP_INTERVAL:       return (int)offsetof(eglConfig, minSwapInterval);
    case EGL_MAX_SWAP_INTERVAL:       return (int)offsetof(eglConfig, maxSwapInterval);
    default:                          return -1;
    }
}

int eglIsValidConfigID(int configID)
{
    eglConfig config;
    if (configID <= 0 || configID > EGL_NUM_BUFFER_FORMATS)
        return 0;
    eglFillConfig(&config, configID);
    return config.surfaceType != 0;
}

int eglIsEmptyList(const EGLint *attribList)
{
    return attribList == NULL || *attribList == EGL_NONE;
}

int eglIsValidDevice(void)
{
    return 1;
}

/* --- HC compiler setjmp / longjmp wrappers --------------------------------- */

int eglHCWrapSetjmp(void *jmpbuf, int (*func)(void *), void *args)
{
    if (setjmp(*(jmp_buf *)jmpbuf) != 0)
        return -1;
    return func(args);
}

void eglHCWrapLongjmp(void *jmpbuf, int val)
{
    longjmp(*(jmp_buf *)jmpbuf, val);
}

int eglGetJmpbufSize(void)
{
    return (hgMax32(64, (int)sizeof(jmp_buf)) + 7) & ~7;
}

/* --- EGL 1.1 Display & Lifecycle ------------------------------------------- */

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(NativeDisplayType display_id)
{
    eglSetError(EGL_SUCCESS);
    if (display_id == EGL_DEFAULT_DISPLAY && eglIsValidDevice() != 0)
        return (EGLDisplay)1;
    return EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    eglState_s *state = egliGetState();

    eglSetError(EGL_SUCCESS);
    if (state == NULL)
    {
#if !defined(__SYMBIAN32__)
        eglDisplay_s *display;
#endif

        state = egliInitState();
        if (state == NULL)
            return EGL_FALSE;

#if !defined(__SYMBIAN32__)
        display = (eglDisplay_s *)eglAlloc(sizeof(eglDisplay_s));
        if (display == NULL)
        {
            egliDestroyState();
            return EGL_FALSE;
        }

        hgMemSet(display, 0, sizeof(*display));
        eglInitRefCount(&display->refCount);
        state->windowDisplay = display;
#endif
        EGL_ASSERT_AT("eglBase.inl", 336, "eglIsValidState(state)", eglIsValidState(state) != 0);
    }

    /* The original creates state before validating the display and keeps it
     * alive when the display is invalid. */
    if (eglMapDisplay(dpy) == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    eglSetError(EGL_SUCCESS);
    if (major != NULL)
        *major = 1;
    if (minor != NULL)
        *minor = 1;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
        return EGL_TRUE;

    eglSetError(EGL_SUCCESS);
    if (dpy != 1)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }

    if (state != NULL)
    {
        eglDisplay_s *display = state->windowDisplay;
        eglContext_s *ctx;
        eglSurface_s *surf;

        EGL_ASSERT_AT("eglBase.inl", 362, "eglIsValidState(state)", eglIsValidState(state) != 0);
#if !defined(__SYMBIAN32__)
        eglMakeCurrent((EGLDisplay)1, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif

        surf = display->surfaceList;
        while (surf != NULL)
        {
            eglSurface_s *nextSurf = surf->next;
            if (eglDestroyRef(&surf->refCount) != 0)
                eglFreeSurface(surf);
            surf = nextSurf;
        }

        ctx = display->contextList;
        while (ctx != NULL)
        {
            eglContext_s *nextCtx = ctx->next;
            if (eglDestroyRef(&ctx->refCount) != 0)
                eglFreeContext(ctx);
            ctx = nextCtx;
        }

        eglDestroyRef(&display->refCount);
#if !defined(__SYMBIAN32__)
        EGL_ASSERT_AT("eglBase.inl", 390, "!display->surfaceList && !display->contextList",
                      display->surfaceList == NULL && display->contextList == NULL);
        eglFree(display);
        state->windowDisplay = NULL;
#endif
        egliDestroyState();
    }

    return EGL_TRUE;
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
    static const char s_vendor[]     = "Hybrid Graphics, Ltd.";
    static const char s_version[]    = "1.1";
    static const char s_extensions[] = "";

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return NULL;
    }

    eglSetError(EGL_SUCCESS);
    if (eglMapDisplay(dpy) == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return NULL;
    }

    switch (name)
    {
    case EGL_VENDOR:     return s_vendor;
    case EGL_VERSION:    return s_version;
    case EGL_EXTENSIONS: return s_extensions;
    default:
        eglSetError(EGL_BAD_PARAMETER);
        return NULL;
    }
}

/* --- EGL 1.1 Configs ------------------------------------------------------- */

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    int totalConfigs, i, count = 0;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    if (eglMapDisplay(dpy) == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (num_config == NULL)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    totalConfigs = egliGetTotalConfigs();
    for (i = 1; i <= totalConfigs && (configs == NULL || count < config_size); ++i)
    {
        if (eglIsValidConfigID(i))
        {
            if (configs != NULL)
                configs[count] = (EGLConfig)i;
            ++count;
        }
    }
    *num_config = count;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                              EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    eglConfig filter;
    int totalConfigs;
    int count = 0;
    int i;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    /* The original leaves the current EGL error unchanged on success. */
    if (eglMapDisplay(dpy) == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (num_config == NULL)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    filter = s_defaultFilter;
    if (attrib_list != NULL)
    {
        while (*attrib_list != EGL_NONE)
        {
            EGLint attr = attrib_list[0];
            EGLint val  = attrib_list[1];
            int offset  = eglGetConfigAttributeOffset(attr);

            if (offset == -1)
            {
                eglSetError(EGL_BAD_ATTRIBUTE);
                return EGL_FALSE;
            }

            switch (attr)
            {
            case EGL_BUFFER_SIZE:
            case EGL_ALPHA_SIZE:
            case EGL_BLUE_SIZE:
            case EGL_GREEN_SIZE:
            case EGL_RED_SIZE:
            case EGL_DEPTH_SIZE:
            case EGL_STENCIL_SIZE:
            case EGL_CONFIG_ID:
            case EGL_SAMPLES:
            case EGL_SAMPLE_BUFFERS:
            case EGL_TRANSPARENT_BLUE_VALUE:
            case EGL_TRANSPARENT_GREEN_VALUE:
            case EGL_TRANSPARENT_RED_VALUE:
            case EGL_MIN_SWAP_INTERVAL:
            case EGL_MAX_SWAP_INTERVAL:
            case EGL_NATIVE_VISUAL_TYPE:
                if (val != EGL_DONT_CARE && val < 0)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            case EGL_CONFIG_CAVEAT:
                if (val != EGL_DONT_CARE && val != EGL_NONE &&
                    val != EGL_SLOW_CONFIG && val != EGL_NON_CONFORMANT_CONFIG)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            case EGL_LEVEL:
                if (val < 0)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            case EGL_NATIVE_RENDERABLE:
            case EGL_BIND_TO_TEXTURE_RGB:
            case EGL_BIND_TO_TEXTURE_RGBA:
                if (val != EGL_DONT_CARE && val != EGL_FALSE && val != EGL_TRUE)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            case EGL_SURFACE_TYPE:
                if (val != EGL_DONT_CARE &&
                    (val & ~(EGL_PBUFFER_BIT | EGL_PIXMAP_BIT | EGL_WINDOW_BIT)) != 0)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            case EGL_TRANSPARENT_TYPE:
                if (val != EGL_DONT_CARE && val != EGL_NONE && val != EGL_TRANSPARENT_RGB)
                {
                    eglSetError(EGL_BAD_ATTRIBUTE);
                    return EGL_FALSE;
                }
                break;

            default:
                break;
            }

            *(int *)((char *)&filter + offset) = val;
            attrib_list += 2;
        }
    }

    totalConfigs = egliGetTotalConfigs();
    /* ARM 0x98CC (x86 0x403D15) stops on equality before sorting the selected prefix.
     * A negative size therefore does not limit the original's output. */
    for (i = 0; i < totalConfigs && (configs == NULL || count != config_size); ++i)
    {
        eglConfig cfg;
        int configID = i + 1;

        eglFillConfig(&cfg, configID);
        if (eglFilterConfig(&cfg, &filter) != 0)
        {
            if (configs != NULL)
                configs[count] = (EGLConfig)configID;
            ++count;
        }
    }

    *num_config = count;
    if (configs != NULL && count > 1)
        egliSortConfigs(configs, count, &filter);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
    eglConfig cfg;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    if (eglMapDisplay(dpy) == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (eglIsValidConfigID(config) == 0)
    {
        eglSetError(EGL_BAD_CONFIG);
        return EGL_FALSE;
    }
    if (value == NULL)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    eglFillConfig(&cfg, config);
    if (attribute == EGL_MAX_PBUFFER_WIDTH || attribute == EGL_MAX_PBUFFER_HEIGHT)
    {
        *value = EGL_MAX_PBUFFER_DIM;
    }
    else if (attribute == EGL_MAX_PBUFFER_PIXELS)
    {
        *value = EGL_MAX_PBUFFER_PIX;
    }
    else
    {
        int offset = eglGetConfigAttributeOffset(attribute);
        if (offset == -1)
        {
            eglSetError(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
        }
        *value = *(const int *)((const char *)&cfg + offset);
    }

    return EGL_TRUE;
}

/* --- EGL 1.1 Window, Pixmap, Surface Queries & Render-to-Texture ----------- */

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     NativeWindowType win, const EGLint *attrib_list)
{
    eglDisplay_s *display;
    eglConfig cfg;
    eglSurface_s *existing;
    eglWindowSurface *winSurf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_NO_SURFACE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (eglIsValidConfigID(config) == 0)
    {
        eglSetError(EGL_BAD_CONFIG);
        return EGL_NO_SURFACE;
    }

    eglFillConfig(&cfg, config);
    if ((cfg.surfaceType & EGL_WINDOW_BIT) == 0)
    {
        eglSetError(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    if (eglIsEmptyList(attrib_list) == 0)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SURFACE;
    }
    if (egliIsValidNativeWindow((eglNativeWindow_s *)win) == 0)
    {
        eglSetError(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }

    if (win != NULL)
    {
        for (existing = display->surfaceList; existing != NULL; existing = existing->next)
        {
            if (existing->type == EGL_SURFACE_WINDOW &&
                ((eglWindowSurface *)existing)->nativeWindow == (eglNativeWindow_s *)win)
            {
                eglSetError(EGL_BAD_ALLOC);
                return EGL_NO_SURFACE;
            }
        }
    }

    winSurf = (eglWindowSurface *)eglAlloc(sizeof(eglWindowSurface));
    if (winSurf == NULL)
    {
        eglSetError(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    hgMemSet(winSurf, 0, sizeof(*winSurf));
    eglInitRefCount(&winSurf->surface.refCount);
    winSurf->surface.display = display;
    winSurf->surface.configID = config;
    winSurf->surface.type = EGL_SURFACE_WINDOW;
    winSurf->nativeWindow = (eglNativeWindow_s *)win;

    if (egliInitWindowSurface(winSurf) == 0 ||
        eglUpdateSurfaceBuffers(&winSurf->surface) == 0)
    {
        egliWindowSurfaceDestroy(winSurf);
        if (winSurf->surface.depthBuffer != NULL)
            eglFree(winSurf->surface.depthBuffer);
        if (winSurf->surface.stencilBuffer != NULL)
            eglFree(winSurf->surface.stencilBuffer);
        eglFree(winSurf);
        eglSetError(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    eglDisplayAddSurface(display, &winSurf->surface);
    return winSurf->surface.id;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                     NativePixmapType pixmap, const EGLint *attrib_list)
{
    eglDisplay_s *display;
    eglConfig cfg;
    eglSurface_s *existing;
    eglPixmapSurface *pixSurf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_NO_SURFACE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_NO_SURFACE;
    }
    if (eglIsValidConfigID(config) == 0)
    {
        eglSetError(EGL_BAD_CONFIG);
        return EGL_NO_SURFACE;
    }

    eglFillConfig(&cfg, config);
    if ((cfg.surfaceType & EGL_PIXMAP_BIT) == 0 ||
        egliConfigPixmapMatch(config, (eglNativePixmap_s *)pixmap) == 0)
    {
        eglSetError(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    if (eglIsEmptyList(attrib_list) == 0)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SURFACE;
    }
    if (egliIsValidNativePixmap((eglNativePixmap_s *)pixmap) == 0)
    {
        eglSetError(EGL_BAD_NATIVE_PIXMAP);
        return EGL_NO_SURFACE;
    }

    for (existing = display->surfaceList; existing != NULL; existing = existing->next)
    {
        if (existing->type == EGL_SURFACE_PIXMAP &&
            ((eglPixmapSurface *)existing)->nativePixmap == (eglNativePixmap_s *)pixmap)
        {
            eglSetError(EGL_BAD_ALLOC);
            return EGL_NO_SURFACE;
        }
    }

    pixSurf = (eglPixmapSurface *)eglAlloc(sizeof(eglPixmapSurface));
    if (pixSurf == NULL)
    {
        eglSetError(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    hgMemSet(pixSurf, 0, sizeof(*pixSurf));
    eglInitRefCount(&pixSurf->surface.refCount);
    pixSurf->surface.display = display;
    pixSurf->surface.configID = config;
    pixSurf->surface.type = EGL_SURFACE_PIXMAP;
    pixSurf->nativePixmap = (eglNativePixmap_s *)pixmap;

    if (egliInitPixmapSurface(pixSurf) == 0 ||
        eglUpdateSurfaceBuffers(&pixSurf->surface) == 0)
    {
        egliPixmapSurfaceDestroy(pixSurf);
        if (pixSurf->surface.depthBuffer != NULL)
            eglFree(pixSurf->surface.depthBuffer);
        if (pixSurf->surface.stencilBuffer != NULL)
            eglFree(pixSurf->surface.stencilBuffer);
        eglFree(pixSurf);
        eglSetError(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }

    eglDisplayAddSurface(display, &pixSurf->surface);
    return pixSurf->surface.id;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    eglDisplay_s *display;
    eglSurface_s *surf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    if (eglDestroyRef(&surf->refCount) != 0)
        eglFreeSurface(surf);

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
    eglDisplay_s *display;
    eglSurface_s *surf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    /* Both originals reject the attribute before touching the mapped surface.
     * A valid attribute with a missing surface dereferences NULL there; retain
     * the checked failure below for that undefined case. */
    if (attribute != EGL_MIPMAP_LEVEL)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    if (surf->type == EGL_SURFACE_PBUFFER)
    {
        eglPbufferSurface *pbuf = (eglPbufferSurface *)surf;
        if (pbuf->textureFormat != EGL_NO_TEXTURE && pbuf->textureTarget != EGL_NO_TEXTURE)
            pbuf->mipmapLevel = hgClamp32(value, 0, 11);
    }
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
    eglDisplay_s *display;
    eglSurface_s *surf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (value == NULL)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    switch (attribute)
    {
    case EGL_CONFIG_ID:
        *value = surf->configID;
        break;
    case EGL_HEIGHT:
        *value = surf->height;
        break;
    case EGL_WIDTH:
        *value = surf->width;
        break;
    case EGL_LARGEST_PBUFFER:
        if (surf->type == EGL_SURFACE_PBUFFER)
            *value = ((eglPbufferSurface *)surf)->isLargestPbuffer;
        break;
    case EGL_TEXTURE_FORMAT:
        if (surf->type == EGL_SURFACE_PBUFFER)
            *value = ((eglPbufferSurface *)surf)->textureFormat;
        break;
    case EGL_TEXTURE_TARGET:
        if (surf->type == EGL_SURFACE_PBUFFER)
            *value = ((eglPbufferSurface *)surf)->textureFormat == EGL_NO_TEXTURE
                   ? EGL_NO_TEXTURE : EGL_TEXTURE_2D;
        break;
    case EGL_MIPMAP_TEXTURE:
        if (surf->type == EGL_SURFACE_PBUFFER)
            *value = ((eglPbufferSurface *)surf)->mipmapTexture;
        break;
    case EGL_MIPMAP_LEVEL:
        if (surf->type == EGL_SURFACE_PBUFFER)
            *value = ((eglPbufferSurface *)surf)->mipmapLevel;
        break;
    default:
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    if (engine != EGL_CORE_NATIVE_ENGINE)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    egliInvalidateWindowSurface(state->currentDrawSurface);
    egliInvalidateWindowSurface(state->currentReadSurface);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    eglState_s *state = egliGetState();
    eglDisplay_s *display;
    eglSurface_s *surf;
    eglPbufferSurface *pbuf;
    eglConfig cfg;
    unsigned int glFormat;
    unsigned int texBinding = 0;

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL || surf->type != EGL_SURFACE_PBUFFER)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (buffer != EGL_BACK_BUFFER)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }

    pbuf = (eglPbufferSurface *)surf;
    if (pbuf->textureFormat == EGL_NO_TEXTURE)
    {
        eglSetError(EGL_BAD_MATCH);
        return EGL_FALSE;
    }
    if (pbuf->boundESContext != NULL)
    {
        eglSetError(EGL_BAD_ACCESS);
        return EGL_FALSE;
    }

    eglFillConfig(&cfg, surf->configID);
    if (cfg.bindToTextureRgb == 0 && cfg.bindToTextureRgba == 0)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    glFlush();

    if (state->currentESContext != NULL)
    {
        glFormat = (pbuf->textureFormat == EGL_TEXTURE_RGB) ? GL_RGB : GL_RGBA;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint *)&texBinding);
        if (texBinding != 0)
        {
            if (oglSetSurfaceTexture(state->currentESContext,
                                     surf->colorBuffer,
                                     surf->width,
                                     surf->height,
                                     glFormat,
                                     pbuf->mipmapLevel,
                                     pbuf->mipmapTexture) != 0)
            {
                pbuf->boundESContext = state->currentESContext;
                pbuf->textureName = texBinding;
            }
        }
    }

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    eglDisplay_s *display;
    eglSurface_s *surf;
    eglPbufferSurface *pbuf;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL || surf->type != EGL_SURFACE_PBUFFER)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (buffer != EGL_BACK_BUFFER)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }

    pbuf = (eglPbufferSurface *)surf;
    if (pbuf->boundESContext == NULL)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    if (oglIsSurfaceTextureReleased(pbuf->boundESContext, pbuf->textureName) == 0)
        oglReleaseSurfaceTexture(pbuf->boundESContext, pbuf->textureName, pbuf->mipmapLevel);
    pbuf->boundESContext = NULL;
    pbuf->textureName = 0;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    eglState_s *state = egliGetState();
    eglDisplay_s *display;
    eglSurface_s *surf;

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL || state->currentDrawSurface != surf)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }

    glFlush();
    if (surf->type == EGL_SURFACE_WINDOW)
        egliSwapWindowSurface((eglWindowSurface *)surf);

    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    eglState_s *state = egliGetState();
    eglDisplay_s *display;

    (void)interval;
    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (state->currentContext == NULL)
    {
        eglSetError(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (state->currentReadSurface == NULL || state->currentDrawSurface == NULL)
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, NativePixmapType target)
{
    eglState_s *state = egliGetState();
    eglDisplay_s *display;
    eglSurface_s *surf;
    int width, height;

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    surf = eglMapSurface(display, surface);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (surf == NULL || (surf != state->currentDrawSurface && surf != state->currentReadSurface))
    {
        eglSetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (egliIsValidNativePixmap((eglNativePixmap_s *)target) == 0)
    {
        eglSetError(EGL_BAD_NATIVE_PIXMAP);
        return EGL_FALSE;
    }

    egliGetNativePixmapSize((eglNativePixmap_s *)target, &width, &height);
    if (width != surf->width || height != surf->height)
    {
        eglSetError(EGL_BAD_MATCH);
        return EGL_FALSE;
    }

    glFlush();
    return egliBlitToPixmap(surf, (eglNativePixmap_s *)target);
}

/* --- EGL 1.1 Current State Queries & eglGetProcAddress --------------------- */

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_NO_CONTEXT;
    }

    eglSetError(EGL_SUCCESS);
    if (state->currentContext != NULL)
        return state->currentContext->id;
    return EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_NO_SURFACE;
    }

    eglSetError(EGL_SUCCESS);
    if (readdraw == EGL_READ)
        return (state->currentReadSurface != NULL) ? state->currentReadSurface->id : EGL_NO_SURFACE;
    if (readdraw == EGL_DRAW)
        return (state->currentDrawSurface != NULL) ? state->currentDrawSurface->id : EGL_NO_SURFACE;

    eglSetError(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
    eglState_s *state = egliGetState();

    if (state == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_NO_DISPLAY;
    }

    eglSetError(EGL_SUCCESS);
    if (state->currentDisplay != NULL)
        return (EGLDisplay)1;
    return EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
    eglDisplay_s *display;
    eglContext_s *context;

    if (egliGetState() == NULL)
    {
        eglSetError(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }

    eglSetError(EGL_SUCCESS);
    display = eglMapDisplay(dpy);
    context = eglMapContext(display, ctx);
    if (display == NULL)
    {
        eglSetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (context == NULL)
    {
        eglSetError(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (attribute != EGL_CONFIG_ID)
    {
        eglSetError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
    }
    if (value == NULL)
    {
        eglSetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }

    *value = context->configID;
    return EGL_TRUE;
}

typedef void (*eglProcPtr_t)(void);

typedef struct eglProcedureEntry_s
{
    const char  *name;
    eglProcPtr_t func;
} eglProcedureEntry_s;

/* PE .rdata 0x470220..0x4702AF (18 OES / ES 1.1 procedures) */
static const eglProcedureEntry_s s_procedures[18] =
{
    { "glWeightPointerOES",                  (eglProcPtr_t)glWeightPointerOES },
    { "glMatrixIndexPointerOES",             (eglProcPtr_t)glMatrixIndexPointerOES },
    { "glPointSizePointerOES",               (eglProcPtr_t)glPointSizePointerOES },
    { "glCurrentPaletteMatrixOES",           (eglProcPtr_t)glCurrentPaletteMatrixOES },
    { "glLoadPaletteFromModelViewMatrixOES", (eglProcPtr_t)glLoadPaletteFromModelViewMatrixOES },
    { "glDrawTexfOES",                       (eglProcPtr_t)glDrawTexfOES },
    { "glDrawTexxOES",                       (eglProcPtr_t)glDrawTexxOES },
    { "glDrawTexiOES",                       (eglProcPtr_t)glDrawTexiOES },
    { "glDrawTexsOES",                       (eglProcPtr_t)glDrawTexsOES },
    { "glDrawTexfvOES",                      (eglProcPtr_t)glDrawTexfvOES },
    { "glDrawTexxvOES",                      (eglProcPtr_t)glDrawTexxvOES },
    { "glDrawTexivOES",                      (eglProcPtr_t)glDrawTexivOES },
    { "glDrawTexsvOES",                      (eglProcPtr_t)glDrawTexsvOES },
    { "glTexParameteri",                     (eglProcPtr_t)glTexParameteri },
    { "glTexParameterfv",                    (eglProcPtr_t)glTexParameterfv },
    { "glTexParameterxv",                    (eglProcPtr_t)glTexParameterxv },
    { "glTexParameteriv",                    (eglProcPtr_t)glTexParameteriv },
    { "glQueryMatrixxOES",                   (eglProcPtr_t)glQueryMatrixxOES }
};

EGLAPI void (*EGLAPIENTRY eglGetProcAddress(const char *procname))(void)
{
    int i;

    if (procname == NULL)
        return NULL;

    for (i = 0; i < 18; ++i)
    {
        if (strcmp(procname, s_procedures[i].name) == 0)
            return s_procedures[i].func;
    }
    return NULL;
}
