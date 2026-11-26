/* eglSymbian.c - native Symbian and hosted platform backends for Gerbera EGL 1.1
 * (ported from eglSymbian.inl & PE .rdata s_validColors at 0x4702B4).
 *
 * The Symbian branch uses Dll::SetTls, UserHeap, CFbsBitmap, RWindow, and
 * CWindowGc through the platform wrappers. Hosted builds provide TLS,
 * native-surface descriptors, and platform-specific executable allocation.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SYMBIAN32__
#include "symbian_os.h"
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0x20
#endif
#endif
#endif

#include "gerbera/egl/eglInternal.h"
#include "gerbera/grState.h"
#include "gerbera/hc/hcBackend.h"
#include "hg/hgInt32.h"
#include "hg/hgMemory.h"

/* --- PE .rdata s_validColors (0x4702B4..0x4702E3) -------------------------- */
struct Info
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
    int           displayMode;
};

static const struct Info s_validColors[6] =
{
    { 8, 8, 8, 8, EGL_SYMBIAN_ECOLOR16MU },
    { 8, 8, 8, 8, EGL_SYMBIAN_ECOLOR16MA },
    { 5, 6, 5, 0, EGL_SYMBIAN_ECOLOR64K  },
    { 8, 8, 8, 0, EGL_SYMBIAN_ECOLOR16MU },
    { 8, 8, 8, 0, EGL_SYMBIAN_ECOLOR16MA },
    { 4, 4, 4, 0, EGL_SYMBIAN_ECOLOR4K   }
};

static eglNativePixmap_s *egliSurfaceBitmap(eglSurface_s *surface)
{
    if (surface == NULL)
        return NULL;
    if (surface->type == EGL_SURFACE_WINDOW)
        return ((eglWindowSurface *)surface)->backBufferBitmap;
    if (surface->type == EGL_SURFACE_PIXMAP)
        return ((eglPixmapSurface *)surface)->nativePixmap;
    return NULL;
}

/* Cache window dimensions until a native synchronization or presentation
 * boundary. Bitmap heap addresses are reacquired on every lock. */
void egliInvalidateWindowSurface(eglSurface_s *surface)
{
    if (surface != NULL && surface->type == EGL_SURFACE_WINDOW)
        ((eglWindowSurface *)surface)->sizeValidated = 0;
}

#ifdef __SYMBIAN32__

/* ===========================================================================
 * Native Symbian OS v9.x (EKA2 / S60 3rd Ed+) Platform Backend
 * Zero Writable Static Data (WSD); matches docs/libgles_cm.dll.epoc ABI.
 * =========================================================================== */

void egliSetDefaultDisplayMode(int displayMode)
{
    (void)displayMode;
}

void egliSetGlobal(void *data)
{
    (void)sym_DllSetTls(data);
}

void *egliGetGlobal(void)
{
    return sym_DllTls();
}

eglState_s *egliGetState(void)
{
    void *state = egliGetGlobal();

    if ((uintptr_t)state <= 3u)
        return NULL;
    return (eglState_s *)state;
}

void *eglAlloc(unsigned int size)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglSymbian.inl", 252, "size > 0", size > 0);
    if (state == NULL || state->heap == NULL)
        return NULL;
    return sym_RHeap_Alloc(state->heap, (int)size);
}

void eglFree(void *ptr)
{
    if (ptr != NULL)
    {
        eglState_s *state = egliGetState();
        if (state != NULL && state->heap != NULL)
            sym_RHeap_Free(state->heap, ptr);
    }
}

struct grExecutable_s
{
    int handle; /* Symbian RChunk (4-byte RHandleBase) */
};

grExecutable_s *eglAllocExecutable(unsigned int size)
{
    grExecutable_s *chunk = (grExecutable_s *)sym_User_Alloc(sizeof(grExecutable_s));

    if (chunk == NULL)
        return NULL;
    chunk->handle = 0;
    if (sym_RChunk_CreateLocalCode(chunk, (int)size, (int)size, 0) != 0)
    {
        sym_User_Free(chunk);
        return NULL;
    }
    return chunk;
}

void eglFreeExecutable(grExecutable_s *executable)
{
    if (executable != NULL)
    {
        sym_RHandleBase_Close(executable);
        sym_User_Free(executable);
    }
}

int eglGetExecutableSize(grExecutable_s *executable)
{
    return sym_RChunk_Size(executable);
}

void *eglGetExecutableData(grExecutable_s *executable)
{
    return sym_RChunk_Base(executable);
}

void *grGetBackend(void)
{
    return HCARMBackend_create();
}

int egliNextPowerOfTwo(int v)
{
    unsigned int u = (unsigned int)v;
    if (u == 0u)
        return 1;
    return 1 << (32 - hgClz32(u));
}

int egliGetTotalConfigs(void)
{
    return EGL_NUM_BUFFER_FORMATS;
}

eglState_s *egliInitState(void)
{
    eglState_s *state;
    eglDisplay_s *display;
    void *coe;
    int maxRam = 0x1000000;
    int numColors = 0;
    int numGrays = 0;

    state = (eglState_s *)sym_User_Alloc((int)sizeof(eglState_s));
    if (state == NULL)
        return NULL;
    if (sym_CCoeEnv_Static() == NULL)
    {
        void *env = sym_User_AllocZL(SYM_CCOEENV_SIZE);
        env = sym_CCoeEnv_Ctor(env);
        sym_CCoeEnv_ConstructTrapped(env);
    }

    display = (eglDisplay_s *)sym_User_Alloc((int)sizeof(*display));
    if (display == NULL)
    {
        sym_User_Free(state);
        return NULL;
    }
    /* ARM 0x92AC clears the ownership flag written before this memset, as
     * well as the initial error. Preserve that original lifecycle bug. */
    hgMemSet(state, 0, sizeof(*state));
    hgMemSet(display, 0, sizeof(*display));
    eglInitRefCount(&display->refCount);
    state->windowDisplay = display;

    if (sym_HAL_Get(SYM_HAL_MEMORY_RAM, &maxRam) != 0 || maxRam <= 0)
        maxRam = 0x1000000;

    state->heap = sym_UserHeap_ChunkHeap(NULL, 0, maxRam, 1, 0, 0);
    if (state->heap == NULL)
    {
        sym_User_Free(state);
        sym_User_Free(display);
        return NULL;
    }

    coe = sym_CCoeEnv_Static();
    if (coe != NULL && SYM_CCOEENV_SCREENDEV(coe) != NULL)
    {
        state->displayMode = sym_RWsSession_GetDefModeMaxNumColors(
            SYM_CCOEENV_WSSESSION(coe), &numColors, &numGrays);
    }
    else
    {
        state->displayMode = 0;
    }

    egliSetGlobal(state);
    return state;
}

__attribute__((externally_visible)) void egliDestroyState(void)
{
    eglState_s *state = egliGetState();

    if (state != NULL)
    {
        if (state->heap != NULL)
        {
            sym_RAllocator_Close(state->heap);
            state->heap = NULL;
        }
        sym_User_Free(state->windowDisplay);
        if (state->ownEnvironment != 0)
        {
            sym_CCoeEnv_DestroyEnvironment(sym_CCoeEnv_Static());
            state->ownEnvironment = 0;
        }
        sym_User_Free(state);
    }
    egliSetGlobal(NULL);
}

int egliIsValidNativeWindow(eglNativeWindow_s *window)
{
    (void)window;
    return 1;
}

int egliIsValidNativePixmap(eglNativePixmap_s *pixmap)
{
    return pixmap != NULL;
}

void egliGetNativeWindowSize(eglWindowSurface *winSurface, int *width, int *height)
{
    SymTSize sz;
    sz.iWidth = 0;
    sz.iHeight = 0;
    if (winSurface != NULL && winSurface->nativeWindow != NULL)
        sym_RWindowBase_Size(&sz, winSurface->nativeWindow);
    *width = sz.iWidth;
    *height = sz.iHeight;
}

void egliGetNativePixmapSize(eglNativePixmap_s *pixmap, int *width, int *height)
{
    SymTSize sz;
    sz.iWidth = 0;
    sz.iHeight = 0;
    if (pixmap != NULL)
        sym_CFbsBitmap_SizeInPixels(&sz, pixmap);
    *width = sz.iWidth;
    *height = sz.iHeight;
}

int egliInitWindowSurface(eglWindowSurface *winSurface)
{
    eglState_s *state = egliGetState();
    void *coe = sym_CCoeEnv_Static();
    void *gc = NULL;
    void *bmp = NULL;
    SymTSize sz;

    if (winSurface == NULL || state == NULL || coe == NULL)
        return 0;

    if (winSurface->nativeWindow != NULL)
    {
        winSurface->ownWindow = 0;
    }
    else
    {
        void *win = sym_User_Alloc(SYM_RWINDOW_SIZE);
        if (win == NULL)
            return 0;
        win = sym_RWindow_Ctor(win, SYM_CCOEENV_WSSESSION(coe));
        winSurface->nativeWindow = (eglNativeWindow_s *)win;
        winSurface->ownWindow = 1;
        if (sym_RWindow_Construct(win, SYM_CCOEENV_ROOTWIN(coe),
                                  (unsigned int)(uintptr_t)winSurface) != 0)
        {
            return 0;
        }
    }

    gc = sym_User_AllocZ(SYM_CWINDOWGC_SIZE);
    if (gc != NULL)
        gc = sym_CWindowGc_Ctor(gc, SYM_CCOEENV_SCREENDEV(coe));

    bmp = sym_User_AllocZ(SYM_CWSBITMAP_SIZE);
    if (bmp != NULL)
        bmp = sym_CWsBitmap_Ctor(bmp, SYM_CCOEENV_WSSESSION(coe));

    sz.iWidth = 0;
    sz.iHeight = 0;
    sym_RWindowBase_Size(&sz, winSurface->nativeWindow);

    if (gc != NULL && sym_CWindowGc_Construct(gc) == 0 &&
        bmp != NULL && sym_CWsBitmap_Create(bmp, &sz, state->displayMode) == 0)
    {
        winSurface->backBufferBitmap = (eglNativePixmap_s *)bmp;
        winSurface->windowGc = gc;
        return 1;
    }

    if (bmp != NULL)
    {
        sym_CWsBitmap_Reset(bmp);
        sym_CBase_Delete(bmp);
    }
    if (gc != NULL)
        sym_CBase_Delete(gc);

    return 0;
}

void egliWindowSurfaceDestroy(eglWindowSurface *winSurface)
{
    eglState_s *state = egliGetState();

    if (winSurface == NULL)
        return;

    if (winSurface->ownWindow != 0 && winSurface->nativeWindow != NULL)
    {
        sym_RWindowTreeNode_Close(winSurface->nativeWindow);
        sym_User_Free(winSurface->nativeWindow);
        winSurface->nativeWindow = NULL;
        winSurface->ownWindow = 0;
    }

    if (winSurface->backBufferBitmap != NULL)
    {
        sym_CFbsBitmap_Reset(winSurface->backBufferBitmap);
        sym_CBase_Delete(winSurface->backBufferBitmap);
        winSurface->backBufferBitmap = NULL;
    }

    if (winSurface->windowGc != NULL)
    {
        sym_CBase_Delete(winSurface->windowGc);
        winSurface->windowGc = NULL;
    }

    winSurface->surface.colorBuffer = NULL;
    if (state != NULL && state->heap != NULL)
        (void)sym_RHeap_Compress(state->heap);
}

int egliConfigPixmapMatch(int configID, eglNativePixmap_s *pixmap)
{
    eglConfig cfg;
    int dispMode;
    int i;

    if (pixmap == NULL)
        return 0;

    dispMode = sym_CFbsBitmap_DisplayMode(pixmap);
    eglFillConfig(&cfg, configID);
    for (i = 0; i < 6; ++i)
    {
        if (cfg.redBits == s_validColors[i].r &&
            cfg.greenBits == s_validColors[i].g &&
            cfg.blueBits == s_validColors[i].b &&
            cfg.alphaBits == s_validColors[i].a &&
            dispMode == s_validColors[i].displayMode)
        {
            return 1;
        }
    }
    return 0;
}

int egliInitPixmapSurface(eglPixmapSurface *pixSurface)
{
    SymTSize sz;
    int dispMode;

    if (pixSurface == NULL || pixSurface->nativePixmap == NULL)
        return 0;

    sz.iWidth = 0;
    sz.iHeight = 0;
    sym_CFbsBitmap_SizeInPixels(&sz, pixSurface->nativePixmap);
    dispMode = sym_CFbsBitmap_DisplayMode(pixSurface->nativePixmap);
    pixSurface->surface.colorBuffer = NULL;
    pixSurface->surface.colorBufferStride = sym_CFbsBitmap_ScanLineLength(sz.iWidth, dispMode);
    return 1;
}

void egliPixmapSurfaceDestroy(eglPixmapSurface *pixSurface)
{
    eglState_s *state = egliGetState();

    if (pixSurface != NULL)
    {
        pixSurface->nativePixmap = NULL;
        pixSurface->surface.colorBuffer = NULL;
    }
    if (state != NULL && state->heap != NULL)
        (void)sym_RHeap_Compress(state->heap);
}

int egliSurfaceResized(eglSurface_s *surface)
{
    if (surface == NULL)
        return 0;

    if (surface->type == EGL_SURFACE_WINDOW)
    {
        egliInvalidateWindowSurface(surface);
        /* Native ARM 0x8B1C updates dimensions only. The lock path acquires the
         * address and stride first, retaining the old stride for one lock. */
        egliGetNativeWindowSize((eglWindowSurface *)surface,
                                &surface->width, &surface->height);
    }
    else if (surface->type == EGL_SURFACE_PIXMAP)
    {
        eglPixmapSurface *pixSurf = (eglPixmapSurface *)surface;
        egliGetNativePixmapSize(pixSurf->nativePixmap,
                                &surface->width, &surface->height);
    }

    return 1;
}

void egliSwapWindowSurface(eglWindowSurface *winSurface)
{
    void *win;
    void *gc;
    void *bmp;
    SymTRect drawRect;
    SymTPoint origin;
    int isEmpty;

    if (winSurface == NULL || winSurface->nativeWindow == NULL ||
        winSurface->windowGc == NULL || winSurface->backBufferBitmap == NULL)
    {
        return;
    }

    egliInvalidateWindowSurface(&winSurface->surface);
    win = winSurface->nativeWindow;
    gc = winSurface->windowGc;
    bmp = winSurface->backBufferBitmap;

    sym_RDrawableWindow_GetDrawRect(&drawRect, win);
    isEmpty = sym_TRect_IsEmpty(&drawRect);
    if (isEmpty != 0)
        sym_RWindow_Invalidate(win);

    sym_CWindowGc_Activate(gc, win);
    if (isEmpty != 0)
        sym_RWindow_BeginRedraw(win);

    origin.iX = 0;
    origin.iY = 0;
    sym_CWindowGc_BitBlt(gc, &origin, bmp);

    if (isEmpty != 0)
        sym_RWindow_EndRedraw(win);
    sym_CWindowGc_Deactivate(gc);
}

int egliBlitToPixmap(eglSurface_s *surface, eglNativePixmap_s *target)
{
    eglConfig cfg;
    int srcBpp;
    int dstDispMode;
    int dstBpp;
    int dstStride;
    SymTSize dstSize;
    int w, h;
    int lockedDraw;
    void *dstData;

    if (surface == NULL || target == NULL || egliConfigPixmapMatch(surface->configID, target) == 0)
    {
        eglSetError(EGL_BAD_MATCH);
        return 0;
    }

    lockedDraw = grLockSurface(GR_SURFACE_DRAW);
    if (lockedDraw == 0)
        sym_CFbsBitmap_LockHeap(target, 0);

    eglFillConfig(&cfg, surface->configID);
    srcBpp = egliNextPowerOfTwo((cfg.colorBits - 1) >> 3);

    if (surface->colorBuffer != NULL)
    {
        dstData = sym_CFbsBitmap_DataAddress(target);
        dstDispMode = sym_CFbsBitmap_DisplayMode(target);
        dstSize.iWidth = 0;
        dstSize.iHeight = 0;
        sym_CFbsBitmap_SizeInPixels(&dstSize, target);
        dstStride = sym_CFbsBitmap_ScanLineLength(dstSize.iWidth, dstDispMode);
        dstBpp = sym_TDisplayModeUtils_NumDisplayModeBitsPerPixel(dstDispMode) >> 3;
        w = hgMin32(surface->width, dstSize.iWidth);
        h = hgMin32(surface->height, dstSize.iHeight);
        if (dstData != NULL && w > 0 && h > 0)
        {
            eglBlit(dstData, dstStride,
                    surface->colorBuffer, surface->colorBufferStride,
                    w, h, srcBpp, dstBpp);
        }
    }

    if (grReleaseSurface(GR_SURFACE_DRAW) == 0)
        sym_CFbsBitmap_UnlockHeap(target, 0);

    return 1;
}

eglNativePixmap_s *eglGetActiveBitmap(int op)
{
    eglState_s *state = egliGetState();
    eglSurface_s *surf;

    if (state == NULL || state->currentDrawSurface == NULL || state->currentReadSurface == NULL)
        return NULL;

    surf = (op == GR_SURFACE_DRAW) ? state->currentDrawSurface : state->currentReadSurface;
    if (surf->type == EGL_SURFACE_WINDOW)
        return ((eglWindowSurface *)surf)->backBufferBitmap;
    if (surf->type == EGL_SURFACE_PIXMAP)
        return ((eglPixmapSurface *)surf)->nativePixmap;
    return NULL;
}

int egliCheckSurfaceSize(eglSurface_s *surface)
{
    int width = 0;
    int height = 0;

    switch (surface->type)
    {
    case EGL_SURFACE_WINDOW:
        egliGetNativeWindowSize((eglWindowSurface *)surface, &width, &height);
        break;
    case EGL_SURFACE_PBUFFER:
        return 0;
    case EGL_SURFACE_PIXMAP:
        egliGetNativePixmapSize(((eglPixmapSurface *)surface)->nativePixmap, &width, &height);
        break;
    default:
        return 0;
    }

    return (width != surface->width || height != surface->height) ? 1 : 0;
}

int egliUpdateSurfaceRenderTarget(eglSurface_s *surface)
{
    void *bmp;
    void *newBuf;
    int dispMode;
    int newStride;
    int changed;

    if (surface->type == EGL_SURFACE_WINDOW)
        bmp = ((eglWindowSurface *)surface)->backBufferBitmap;
    else if (surface->type == EGL_SURFACE_PIXMAP)
        bmp = ((eglPixmapSurface *)surface)->nativePixmap;
    else
        return 0;

    if (bmp == NULL)
        return 0;

    newBuf = sym_CFbsBitmap_DataAddress(bmp);
    dispMode = sym_CFbsBitmap_DisplayMode(bmp);
    newStride = sym_CFbsBitmap_ScanLineLength(surface->width, dispMode);
    changed = (newBuf != surface->colorBuffer || newStride != surface->colorBufferStride) ? 1 : 0;
    surface->colorBuffer = newBuf;
    surface->colorBufferStride = newStride;
    return changed;
}

int grLockSurfaceState(void *tlsState, int op)
{
    eglState_s *state = (eglState_s *)tlsState;
    eglSurface_s *surf;
    eglNativePixmap_s *bmp;
    int sizeChanged;
    int addrChanged;
    int locked = 0;

    if (state == NULL || state->currentDrawSurface == NULL || state->currentReadSurface == NULL)
        return 0;

    surf = (op == GR_SURFACE_DRAW) ? state->currentDrawSurface : state->currentReadSurface;
    bmp = egliSurfaceBitmap(surf);
    if (surf->type == EGL_SURFACE_WINDOW && ((eglWindowSurface *)surf)->sizeValidated)
    {
        if (bmp != NULL)
        {
            void *address;
            sym_CFbsBitmap_LockHeap(bmp, 0);
            locked = 1;
            address = sym_CFbsBitmap_DataAddress(bmp);
            addrChanged = address != surf->colorBuffer;
            surf->colorBuffer = address;
            if (addrChanged && state->currentESContext != NULL)
                eglUpdateSurfaces(state->currentESContext,
                                  state->currentDrawSurface, state->currentReadSurface);
        }
        return locked;
    }
    sizeChanged = egliCheckSurfaceSize(surf);

    if (sizeChanged != 0 && surf->type == EGL_SURFACE_WINDOW)
    {
        eglWindowSurface *winSurf = (eglWindowSurface *)surf;
        if (winSurf->backBufferBitmap != NULL)
        {
            SymTSize newSz;
            egliGetNativeWindowSize(winSurf, &newSz.iWidth, &newSz.iHeight);
            if (sym_CFbsBitmap_Resize(winSurf->backBufferBitmap, &newSz) != 0)
            {
                eglSetError(EGL_BAD_ALLOC);
                return 0;
            }
        }
    }

    if (bmp != NULL)
    {
        sym_CFbsBitmap_LockHeap(bmp, 0);
        locked = 1;
    }

    addrChanged = egliUpdateSurfaceRenderTarget(surf);
    if ((addrChanged | sizeChanged) != 0 && state->currentESContext != NULL)
    {
        eglUpdateSurfaces(state->currentESContext,
                          state->currentDrawSurface,
                          state->currentReadSurface);
    }

    if (!sizeChanged && surf->type == EGL_SURFACE_WINDOW)
        ((eglWindowSurface *)surf)->sizeValidated = 1;
    return locked;
}

int grReleaseSurfaceState(void *tlsState, int op)
{
    eglState_s *state = (eglState_s *)tlsState;
    eglNativePixmap_s *bmp;

    if (state == NULL || state->currentDrawSurface == NULL || state->currentReadSurface == NULL)
        return 0;
    bmp = egliSurfaceBitmap(op == GR_SURFACE_DRAW ? state->currentDrawSurface : state->currentReadSurface);

    if (bmp != NULL)
    {
        sym_CFbsBitmap_UnlockHeap(bmp, 0);
        return 1;
    }
    return 0;
}

#else /* !__SYMBIAN32__ */

/* --- Hosted TLS & default display mode ------------------------------------- */
#if defined(_MSC_VER)
#define EGL_THREAD_LOCAL __declspec(thread)
#else
#define EGL_THREAD_LOCAL __thread
#endif
static EGL_THREAD_LOCAL void *s_tlsSlot = NULL;
static EGL_THREAD_LOCAL eglState_s s_directState;
static int        s_defaultDisplayMode = EGL_SYMBIAN_ECOLOR64K;

void egliSetDefaultDisplayMode(int displayMode)
{
    s_defaultDisplayMode = displayMode;
}

void egliSetGlobal(void *data)
{
    s_tlsSlot = data;
}

void *egliGetGlobal(void)
{
    return s_tlsSlot;
}

eglState_s *egliGetState(void)
{
    void *state = egliGetGlobal();

    if ((uintptr_t)state <= 3u)
        return NULL;
    return (eglState_s *)state;
}

/* Compatibility helper for standalone api_opengl tests without eglInitialize */
void egliSetCurrentContext(oglContext_s *context)
{
    if (context != NULL)
    {
        hgMemSet(&s_directState, 0, sizeof(s_directState));
        s_directState.currentESContext = context;
        s_directState.errorCode = EGL_SUCCESS;
        s_directState.displayMode = s_defaultDisplayMode;
        s_tlsSlot = &s_directState;
    }
    else if (s_tlsSlot == &s_directState)
    {
        s_directState.currentESContext = NULL;
        s_tlsSlot = NULL;
    }
}

/* --- Hosted heap & executable code allocator ------------------------------- */

struct grExecutable_s
{
    void *data;
    int   size;
};

void *eglAlloc(unsigned int size)
{
    EGL_ASSERT_AT("eglSymbian.inl", 252, "size > 0", size > 0);
    return malloc((size_t)size);
}

void eglFree(void *ptr)
{
    if (ptr != NULL)
        free(ptr);
}

grExecutable_s *eglAllocExecutable(unsigned int size)
{
    grExecutable_s *exec;

    EGL_ASSERT_AT("eglSymbian.inl", 281, "size > 0", size > 0);
    exec = (grExecutable_s *)malloc(sizeof(grExecutable_s));
    if (exec == NULL)
        return NULL;
#if (defined(__arm__) || defined(__i386__)) && (defined(__unix__) || defined(__APPLE__))
    {
        size_t mapSize = ((size_t)size + 4095u) & ~(size_t)4095u;
        void *mem = mmap(NULL, mapSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED)
        {
            free(exec);
            return NULL;
        }
        exec->data = mem;
        exec->size = (int)mapSize;
        return exec;
    }
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    exec->data = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (exec->data == NULL)
    {
        free(exec);
        return NULL;
    }
    exec->size = (int)size;
    return exec;
#else
    exec->data = malloc((size_t)size);
    if (exec->data == NULL)
    {
        free(exec);
        return NULL;
    }
    exec->size = (int)size;
    return exec;
#endif
}

void eglFreeExecutable(grExecutable_s *executable)
{
    EGL_ASSERT_AT("eglSymbian.inl", 306, "executable", executable != NULL);
#if (defined(__arm__) || defined(__i386__)) && (defined(__unix__) || defined(__APPLE__))
    munmap(executable->data, (size_t)executable->size);
#elif defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
    VirtualFree(executable->data, 0, MEM_RELEASE);
#else
    free(executable->data);
#endif
    free(executable);
}

int eglGetExecutableSize(grExecutable_s *executable)
{
    EGL_ASSERT_AT("eglSymbian.inl", 321, "executable", executable != NULL);
    return executable->size;
}

void *eglGetExecutableData(grExecutable_s *executable)
{
    EGL_ASSERT_AT("eglSymbian.inl", 334, "executable", executable != NULL);
    return executable->data;
}

void *grGetBackend(void)
{
#ifdef __arm__
    return HCARMBackend_create();
#else
    return HCX86Backend_create();
#endif
}

/* --- Numeric & Config Helpers ---------------------------------------------- */

int egliNextPowerOfTwo(int v)
{
    int u = v | (v >> 1);
    u |= u >> 2;
    u |= u >> 4;
    u |= u >> 8;
    return (u | (u >> 16)) + 1;
}

int egliGetTotalConfigs(void)
{
    return EGL_NUM_BUFFER_FORMATS;
}

eglState_s *egliInitState(void)
{
    eglState_s *state = (eglState_s *)malloc(sizeof(eglState_s));

    if (state == NULL)
        return NULL;

    hgMemSet(state, 0, sizeof(*state));
    state->errorCode = EGL_SUCCESS;
    state->displayMode = s_defaultDisplayMode;
    state->heap = state;
    state->ownEnvironment = 1;
    egliSetGlobal(state);
    return state;
}

void egliDestroyState(void)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglSymbian.inl", 622, "state", state != NULL);
    free(state);
    egliSetGlobal(NULL);
}

/* --- Native Window & Pixmap Backend ---------------------------------------- */

static eglNativePixmap_s *egliCreateBackBufferPixmap(int width, int height, int displayMode, int bytesPerPixel)
{
    eglNativePixmap_s *bmp;
    size_t byteCount;

    bmp = (eglNativePixmap_s *)eglAlloc(sizeof(eglNativePixmap_s));
    if (bmp == NULL)
        return NULL;

    hgMemSet(bmp, 0, sizeof(*bmp));
    bmp->width = width;
    bmp->height = height;
    bmp->stride = width * bytesPerPixel;
    bmp->displayMode = displayMode;
    bmp->locked = 0;

    byteCount = (size_t)bmp->stride * (size_t)height;
    if (byteCount > 0)
    {
        bmp->data = eglAlloc((unsigned int)byteCount);
        if (bmp->data == NULL)
        {
            eglFree(bmp);
            return NULL;
        }
        hgMemSet(bmp->data, 0, (int)byteCount);
    }
    return bmp;
}

static void egliDestroyBackBufferPixmap(eglNativePixmap_s *bmp)
{
    if (bmp == NULL)
        return;
    if (bmp->data != NULL)
        eglFree(bmp->data);
    eglFree(bmp);
}

int egliIsValidNativeWindow(eglNativeWindow_s *window)
{
    if (window == NULL)
        return 1; /* Symbian / Gerbera allows NULL window -> owned default window */
    if (window->width < 0 || window->height < 0)
        return 0;
    return 1;
}

int egliIsValidNativePixmap(eglNativePixmap_s *pixmap)
{
    if (pixmap == NULL)
        return 0;
    if (pixmap->width < 0 || pixmap->height < 0)
        return 0;
    if (pixmap->width > 0 && pixmap->height > 0 && pixmap->data == NULL)
        return 0;
    return 1;
}

void egliGetNativeWindowSize(eglWindowSurface *winSurface, int *width, int *height)
{
    EGL_ASSERT_AT("eglSymbian.inl", 1111, "winSurface && width && height",
                  winSurface != NULL && width != NULL && height != NULL);
    if (winSurface->nativeWindow != NULL)
    {
        *width = winSurface->nativeWindow->width;
        *height = winSurface->nativeWindow->height;
    }
    else
    {
        *width = 0;
        *height = 0;
    }
}

void egliGetNativePixmapSize(eglNativePixmap_s *pixmap, int *width, int *height)
{
    EGL_ASSERT_AT("eglSymbian.inl", 1134, "egliIsValidNativePixmap(pixmap) && width && height",
                  egliIsValidNativePixmap(pixmap) != 0 && width != NULL && height != NULL);
    *width = pixmap->width;
    *height = pixmap->height;
}

int egliInitWindowSurface(eglWindowSurface *winSurface)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglSymbian.inl", 734, "winSurface && state",
                  winSurface != NULL && state != NULL);

    if (winSurface->nativeWindow == NULL)
    {
        eglNativeWindow_s *owned = (eglNativeWindow_s *)eglAlloc(sizeof(eglNativeWindow_s));
        if (owned == NULL)
            return 0;
        hgMemSet(owned, 0, sizeof(*owned));
        owned->width = 320;
        owned->height = 240;
        owned->displayMode = state->displayMode;
        owned->stride = 0;
        winSurface->nativeWindow = owned;
        winSurface->ownWindow = 1;
    }
    else
    {
        winSurface->ownWindow = 0;
        if (winSurface->nativeWindow->displayMode == 0)
            winSurface->nativeWindow->displayMode = state->displayMode;
    }

    winSurface->windowGc = winSurface->nativeWindow;
    return 1;
}

void egliWindowSurfaceDestroy(eglWindowSurface *winSurface)
{
    if (winSurface == NULL)
        return;

    if (winSurface->backBufferBitmap != NULL)
    {
        egliDestroyBackBufferPixmap(winSurface->backBufferBitmap);
        winSurface->backBufferBitmap = NULL;
    }

    if (winSurface->ownWindow != 0 && winSurface->nativeWindow != NULL)
    {
        eglFree(winSurface->nativeWindow);
        winSurface->nativeWindow = NULL;
        winSurface->ownWindow = 0;
    }
    winSurface->windowGc = NULL;
    winSurface->surface.colorBuffer = NULL;
}

int egliConfigPixmapMatch(int configID, eglNativePixmap_s *pixmap)
{
    eglConfig cfg;
    int i;

    if (egliIsValidNativePixmap(pixmap) == 0)
        return 0;

    eglFillConfig(&cfg, configID);
    for (i = 0; i < 6; ++i)
    {
        if (cfg.redBits == s_validColors[i].r &&
            cfg.greenBits == s_validColors[i].g &&
            cfg.blueBits == s_validColors[i].b &&
            cfg.alphaBits == s_validColors[i].a &&
            pixmap->displayMode == s_validColors[i].displayMode)
        {
            return 1;
        }
    }
    return 0;
}

int egliInitPixmapSurface(eglPixmapSurface *pixSurface)
{
    EGL_ASSERT_AT("eglSymbian.inl", 1042, "pixSurface && egliIsValidNativePixmap(pixSurface->nativePixmap)",
                  pixSurface != NULL && egliIsValidNativePixmap(pixSurface->nativePixmap) != 0);

    pixSurface->surface.colorBuffer = pixSurface->nativePixmap->data;
    pixSurface->surface.colorBufferStride = pixSurface->nativePixmap->stride;
    return 1;
}

void egliPixmapSurfaceDestroy(eglPixmapSurface *pixSurface)
{
    if (pixSurface != NULL)
    {
        pixSurface->nativePixmap = NULL;
        pixSurface->surface.colorBuffer = NULL;
    }
}

int egliSurfaceResized(eglSurface_s *surface)
{
    EGL_ASSERT_AT("eglSymbian.inl", 1495, "surface", surface != NULL);

    if (surface->type == EGL_SURFACE_WINDOW)
    {
        eglWindowSurface *winSurf = (eglWindowSurface *)surface;
        eglConfig cfg;
        int bytesPerPixel;
        int displayMode;

        egliInvalidateWindowSurface(surface);

        if (winSurf->backBufferBitmap != NULL)
        {
            egliDestroyBackBufferPixmap(winSurf->backBufferBitmap);
            winSurf->backBufferBitmap = NULL;
        }
        surface->colorBuffer = NULL;
        surface->colorBufferStride = 0;

        if (surface->width == 0 || surface->height == 0)
            return 1;

        eglFillConfig(&cfg, surface->configID);
        bytesPerPixel = egliNextPowerOfTwo((cfg.colorBits - 1) >> 3);
        displayMode = (bytesPerPixel == 4)
                    ? ((cfg.alphaBits > 0) ? EGL_SYMBIAN_ECOLOR16MA : EGL_SYMBIAN_ECOLOR16MU)
                    : ((cfg.redBits == 4) ? EGL_SYMBIAN_ECOLOR4K : EGL_SYMBIAN_ECOLOR64K);

        winSurf->backBufferBitmap = egliCreateBackBufferPixmap(surface->width, surface->height,
                                                               displayMode, bytesPerPixel);
        if (winSurf->backBufferBitmap == NULL)
            return 0;

        surface->colorBuffer = winSurf->backBufferBitmap->data;
        surface->colorBufferStride = winSurf->backBufferBitmap->stride;
    }
    else if (surface->type == EGL_SURFACE_PIXMAP)
    {
        eglPixmapSurface *pixSurf = (eglPixmapSurface *)surface;
        if (pixSurf->nativePixmap != NULL)
        {
            surface->colorBuffer = pixSurf->nativePixmap->data;
            surface->colorBufferStride = pixSurf->nativePixmap->stride;
        }
    }

    return 1;
}

void egliSwapWindowSurface(eglWindowSurface *winSurface)
{
    eglNativeWindow_s *win;
    eglNativePixmap_s *back;

    if (winSurface == NULL || winSurface->nativeWindow == NULL)
        return;

    egliInvalidateWindowSurface(&winSurface->surface);
    win = winSurface->nativeWindow;
    back = winSurface->backBufferBitmap;
    ++win->swapCount;

    if (win->frontBuffer != NULL && back != NULL && back->data != NULL &&
        winSurface->surface.width > 0 && winSurface->surface.height > 0)
    {
        eglConfig cfg;
        int bpp;
        int dstStride;

        eglFillConfig(&cfg, winSurface->surface.configID);
        bpp = egliNextPowerOfTwo((cfg.colorBits - 1) >> 3);
        dstStride = (win->stride > 0) ? win->stride : (winSurface->surface.width * bpp);
        eglBlit(win->frontBuffer, dstStride,
                back->data, back->stride,
                winSurface->surface.width, winSurface->surface.height,
                bpp, bpp);
    }
}

int egliBlitToPixmap(eglSurface_s *surface, eglNativePixmap_s *target)
{
    eglConfig cfg;
    int bpp;
    int dstWidth = 0;
    int dstHeight = 0;
    int w, h;
    int lockedDraw;

    EGL_ASSERT_AT("eglSymbian.inl", 1438, "surface && egliIsValidNativePixmap(target)",
                  surface != NULL && egliIsValidNativePixmap(target) != 0);

    if (egliConfigPixmapMatch(surface->configID, target) == 0)
    {
        eglSetError(EGL_BAD_MATCH);
        return 0;
    }

    lockedDraw = grLockSurface(GR_SURFACE_DRAW);
    if (lockedDraw == 0)
        ++target->locked;

    eglFillConfig(&cfg, surface->configID);
    bpp = egliNextPowerOfTwo((cfg.colorBits - 1) >> 3);

    if (surface->colorBuffer != NULL && target->data != NULL)
    {
        egliGetNativePixmapSize(target, &dstWidth, &dstHeight);
        w = hgMin32(surface->width, dstWidth);
        h = hgMin32(surface->height, dstHeight);
        if (w > 0 && h > 0)
        {
            eglBlit(target->data, target->stride,
                    surface->colorBuffer, surface->colorBufferStride,
                    w, h, bpp, bpp);
        }
    }

    if (lockedDraw == 0 && target->locked > 0)
        --target->locked;
    grReleaseSurface(GR_SURFACE_DRAW);
    return 1;
}

/* --- Surface Locking & Render Target Refresh ------------------------------- */

eglNativePixmap_s *eglGetActiveBitmap(int op)
{
    eglState_s *state = egliGetState();
    eglSurface_s *surf;

    if (state == NULL)
        return NULL;

    surf = (op == GR_SURFACE_DRAW) ? state->currentDrawSurface : state->currentReadSurface;
    if (surf == NULL)
        return NULL;

    if (surf->type == EGL_SURFACE_WINDOW)
        return ((eglWindowSurface *)surf)->backBufferBitmap;
    if (surf->type == EGL_SURFACE_PIXMAP)
        return ((eglPixmapSurface *)surf)->nativePixmap;
    return NULL;
}

int egliCheckSurfaceSize(eglSurface_s *surface)
{
    int width = 0;
    int height = 0;

    EGL_ASSERT_AT("eglSymbian.inl", 442, "surface", surface != NULL);
    switch (surface->type)
    {
    case EGL_SURFACE_WINDOW:
        egliGetNativeWindowSize((eglWindowSurface *)surface, &width, &height);
        break;
    case EGL_SURFACE_PBUFFER:
        width = surface->width;
        height = surface->height;
        break;
    case EGL_SURFACE_PIXMAP:
        egliGetNativePixmapSize(((eglPixmapSurface *)surface)->nativePixmap, &width, &height);
        break;
    default:
        break;
    }

    if (width != surface->width || height != surface->height)
    {
        eglState_s *state = egliGetState();
        if (state != NULL && state->currentESContext != NULL)
        {
            eglUpdateSurfaces(state->currentESContext,
                              state->currentDrawSurface,
                              state->currentReadSurface);
            return 1;
        }
    }
    return 0;
}

int egliUpdateSurfaceRenderTarget(eglSurface_s *surface)
{
    void *newBuf = surface->colorBuffer;
    int newStride = surface->colorBufferStride;

    if (surface->type == EGL_SURFACE_WINDOW)
    {
        eglWindowSurface *winSurf = (eglWindowSurface *)surface;
        if (winSurf->backBufferBitmap != NULL)
        {
            newBuf = winSurf->backBufferBitmap->data;
            newStride = winSurf->backBufferBitmap->stride;
        }
    }
    else if (surface->type == EGL_SURFACE_PIXMAP)
    {
        eglPixmapSurface *pixSurf = (eglPixmapSurface *)surface;
        if (pixSurf->nativePixmap != NULL)
        {
            newBuf = pixSurf->nativePixmap->data;
            newStride = pixSurf->nativePixmap->stride;
        }
    }

    if (newBuf != surface->colorBuffer || newStride != surface->colorBufferStride)
    {
        eglState_s *state = egliGetState();
        surface->colorBuffer = newBuf;
        surface->colorBufferStride = newStride;
        if (state != NULL && state->currentESContext != NULL)
        {
            eglUpdateSurfaces(state->currentESContext,
                              state->currentDrawSurface,
                              state->currentReadSurface);
            return 1;
        }
    }
    return 0;
}

int grLockSurfaceState(void *tlsState, int op)
{
    eglState_s *state = (eglState_s *)tlsState;
    eglSurface_s *surf;
    eglNativePixmap_s *bmp;

    EGL_ASSERT_AT("eglSymbian.inl", 482, "op == GR_SURFACE_DRAW || op == GR_SURFACE_READ",
                  op == GR_SURFACE_DRAW || op == GR_SURFACE_READ);

    if (state == NULL || state->currentDrawSurface == NULL || state->currentReadSurface == NULL)
        return 0;

    surf = (op == GR_SURFACE_DRAW) ? state->currentDrawSurface : state->currentReadSurface;
    egliCheckSurfaceSize(surf);
    bmp = egliSurfaceBitmap(surf);
    if (bmp != NULL)
    {
        ++bmp->locked;
        egliUpdateSurfaceRenderTarget(surf);
        return 1;
    }
    return 0;
}

int grReleaseSurfaceState(void *tlsState, int op)
{
    eglState_s *state = (eglState_s *)tlsState;
    eglNativePixmap_s *bmp;

    EGL_ASSERT_AT("eglSymbian.inl", 518, "op == GR_SURFACE_DRAW || op == GR_SURFACE_READ",
                  op == GR_SURFACE_DRAW || op == GR_SURFACE_READ);

    bmp = state == NULL ? NULL :
        egliSurfaceBitmap(op == GR_SURFACE_DRAW ? state->currentDrawSurface : state->currentReadSurface);
    if (bmp != NULL)
    {
        if (bmp->locked > 0)
            --bmp->locked;
        return 1;
    }
    return 0;
}

#endif /* __SYMBIAN32__ */

int grLockSurface(int op)
{
    return grLockSurfaceState(egliGetState(), op);
}

int grReleaseSurface(int op)
{
    return grReleaseSurfaceState(egliGetState(), op);
}

/* --- Symbian Config & Pixel Offset Fixups ---------------------------------- */

void egliFixConfig(eglConfig *config)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglSymbian.inl", 1554, "config", config != NULL);

    /* Match the native bitmap formats and the active screen display mode. */
    if (state != NULL)
    {
        int i;
        if ((config->surfaceType & EGL_PIXMAP_BIT) != 0)
        {
            config->surfaceType &= ~EGL_PIXMAP_BIT;
            for (i = 0; i < 6; ++i)
            {
                if (config->redBits == s_validColors[i].r &&
                    config->greenBits == s_validColors[i].g &&
                    config->blueBits == s_validColors[i].b &&
                    config->alphaBits == s_validColors[i].a)
                {
                    config->surfaceType |= EGL_PIXMAP_BIT;
                    config->nativeRenderable = EGL_TRUE;
                    break;
                }
            }
        }
        if ((config->surfaceType & EGL_WINDOW_BIT) != 0)
        {
            config->surfaceType &= ~EGL_WINDOW_BIT;
            for (i = 0; i < 6; ++i)
            {
                if (config->redBits == s_validColors[i].r &&
                    config->greenBits == s_validColors[i].g &&
                    config->blueBits == s_validColors[i].b &&
                    config->alphaBits == s_validColors[i].a &&
                    state->displayMode == s_validColors[i].displayMode)
                {
                    config->surfaceType |= EGL_WINDOW_BIT;
                    config->nativeRenderable = EGL_TRUE;
                    break;
                }
            }
        }
    }
}

void egliFixPixelOffsets(oglSurfaceDescriptor *desc, const eglSurface_s *surface)
{
    /* On Symbian EColor4K / EColor64K / EColor16MU / EColor16MA, B is at bit 0,
     * G at blueBits, R at blueBits+greenBits, A at blueBits+greenBits+redBits -
     * identical to eglSetSurfaceDescriptor's default layout. */
    (void)desc;
    (void)surface;
}

/* --- EGL Error State (including pre-init TLS sentinel tags) ---------------- */

void eglSetError(int error)
{
    eglState_s *state = egliGetState();

    EGL_ASSERT_AT("eglSymbian.inl", 1609, "error >= EGL_SUCCESS && error <= EGL_CONTEXT_LOST",
                  error >= EGL_SUCCESS && error <= EGL_CONTEXT_LOST);

    if (state != NULL)
    {
        state->errorCode = error;
    }
    else
    {
        uintptr_t tag = 0u;
        if (error == EGL_BAD_DISPLAY)
            tag = 1u;
        else if (error == EGL_NOT_INITIALIZED)
            tag = 2u;
        else
            EGL_ASSERT_AT("eglSymbian.inl", 1626, "error == EGL_SUCCESS", error == EGL_SUCCESS);
        egliSetGlobal((void *)tag);
    }
}

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
    eglState_s *state = egliGetState();

    if (state != NULL)
    {
        int err = state->errorCode;
        state->errorCode = EGL_SUCCESS;
        return err;
    }
    else
    {
        uintptr_t tag = (uintptr_t)egliGetGlobal();
        int err = EGL_SUCCESS;

        if (tag == 1u)
            err = EGL_BAD_DISPLAY;
        else if (tag == 2u)
            err = EGL_NOT_INITIALIZED;
        egliSetGlobal(NULL);
        return err;
    }
}

/* --- Config Sorting (native RArray, hosted insertion sort) ---------------- */

int egliCompareConfigs(const TEGLCachedConfig *config1, const TEGLCachedConfig *config2)
{
    return eglCompareConfigs(&config1->iConfig, &config2->iConfig, config1->iFilter);
}

#ifdef __SYMBIAN32__
static int egliCompareNativeConfigs(const void *a, const void *b)
{
    return egliCompareConfigs((const TEGLCachedConfig *)a, (const TEGLCachedConfig *)b);
}
#endif

void egliSortConfigs(EGLConfig *configs, int configCount, const eglConfig *filter)
{
#ifdef __SYMBIAN32__
    SymRArrayBase array;
    TEGLCachedConfig item;
    int i;

    sym_RArrayBase_Ctor(&array, (int)sizeof(item));
    for (i = 0; i < configCount; ++i)
    {
        item.iFilter = filter;
        eglFillConfig(&item.iConfig, configs[i]);
        (void)sym_RArrayBase_Append(&array, &item);
    }
    sym_RArrayBase_HeapSort(&array, egliCompareNativeConfigs);
    for (i = 0; i < configCount; ++i)
        configs[i] = ((const TEGLCachedConfig *)sym_RArrayBase_At(&array, i))->iConfig.configID;
    sym_RArrayBase_Close(&array);
#else
    TEGLCachedConfig cached[EGL_NUM_BUFFER_FORMATS];
    int i, j;

    if (configs == NULL || configCount <= 1)
        return;

    if (configCount > EGL_NUM_BUFFER_FORMATS)
        configCount = EGL_NUM_BUFFER_FORMATS;

    for (i = 0; i < configCount; ++i)
    {
        TEGLCachedConfig item;
        item.iFilter = filter;
        eglFillConfig(&item.iConfig, configs[i]);

        j = i;
        while (j > 0 && egliCompareConfigs(&cached[j - 1], &item) > 0)
        {
            cached[j] = cached[j - 1];
            --j;
        }
        cached[j] = item;
    }

    for (i = 0; i < configCount; ++i)
        configs[i] = cached[i].iConfig.configID;
#endif
}
