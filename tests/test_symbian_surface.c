/* Exercise the native surface-lock path with a movable Symbian bitmap heap. */
#include <stdio.h>
#include <string.h>

#include "symbian_os.h"
#include "gerbera/egl/eglInternal.h"

/* Keep host pointer sizes in the data structures while compiling the native
 * backend and the real EGL descriptor-update path against the mocks below. */
#define __SYMBIAN32__ 1
#include "../src/gerbera/egl/eglSymbian.c"
#include "../src/gerbera/egl/eglUtil.c"

enum {
    CONFIG_RGBA8_WINDOW = 57,
    CONFIG_RGBA8_PIXMAP = 56,
    CONFIG_RGBA8_WINDOW_DEPTH16_STENCIL8 = 66,
    CONFIG_RGBA8_PIXMAP_DEPTH16_STENCIL8 = 65
};
#include "test_support.h"

typedef struct MockBitmap_s {
    SymTSize size;
    unsigned int *data;
    int locks;
    int resizes;
} MockBitmap;

static eglState_s state;
static unsigned int oldPixels[16 * 16], newPixels[16 * 16];
static oglSurfaceDescriptor installedDraw, installedRead;
static int updates;
static int resizeFails;
static int tlsCalls;
static int windowQueries, bitmapAddresses, displayModeQueries, strideQueries;

typedef struct MockHeapVtable_s {
    void *(*alloc)(void *, int);
    void (*free)(void *, void *);
} MockHeapVtable;

typedef struct MockHeap_s {
    const MockHeapVtable *vtable;
    int calls;
    int failAt;
    int frees;
    void *freed[2];
    unsigned char allocations[2][16 * 16 * 4];
} MockHeap;

static void *mock_alloc(void *ptr, int size)
{
    MockHeap *heap = (MockHeap *)ptr;
    ++heap->calls;
    CHECK(heap->calls <= 2 && size > 0 && size <= (int)sizeof(heap->allocations[0]));
    if (heap->calls == heap->failAt)
        return NULL;
    return heap->allocations[heap->calls - 1];
}

static void mock_free(void *ptr, void *allocation)
{
    MockHeap *heap = (MockHeap *)ptr;
    CHECK(heap->frees < 2);
    heap->freed[heap->frees++] = allocation;
}

static const MockHeapVtable heapVtable = { mock_alloc, mock_free };

void *sym_DllTls(void) { ++tlsCalls; return &state; }
int sym_DllSetTls(void *ptr) { (void)ptr; return 0; }

void sym_RWindowBase_Size(SymTSize *size, const void *window)
{
    ++windowQueries;
    *size = *(const SymTSize *)window;
}

void sym_CFbsBitmap_SizeInPixels(SymTSize *size, const void *bitmap)
{
    *size = ((const MockBitmap *)bitmap)->size;
}

int sym_CFbsBitmap_Resize(void *bitmap, const SymTSize *size)
{
    MockBitmap *bmp = (MockBitmap *)bitmap;
    CHECK(bmp->locks == 0);
    if (resizeFails)
        return -4;
    bmp->size = *size;
    bmp->data = newPixels;
    ++bmp->resizes;
    return 0;
}

void sym_CFbsBitmap_LockHeap(const void *bitmap, int always)
{
    (void)always;
    ++((MockBitmap *)bitmap)->locks;
}

void sym_CFbsBitmap_UnlockHeap(const void *bitmap, int always)
{
    (void)always;
    CHECK(((const MockBitmap *)bitmap)->locks == 1);
    --((MockBitmap *)bitmap)->locks;
}

void *sym_CFbsBitmap_DataAddress(const void *bitmap)
{
    ++bitmapAddresses;
    CHECK(((const MockBitmap *)bitmap)->locks == 1);
    return ((const MockBitmap *)bitmap)->data;
}

int sym_CFbsBitmap_DisplayMode(const void *bitmap)
{
    ++displayModeQueries;
    (void)bitmap;
    return EGL_SYMBIAN_ECOLOR16MA;
}

int sym_CFbsBitmap_ScanLineLength(int width, int mode)
{
    ++strideQueries;
    CHECK(mode == EGL_SYMBIAN_ECOLOR16MA);
    return width * 4;
}

int sym_TDisplayModeUtils_NumDisplayModeBitsPerPixel(int mode)
{
    CHECK(mode == EGL_SYMBIAN_ECOLOR16MA);
    return 32;
}

void oglSetBuffers(oglContext_s *context, const oglSurfaceDescriptor *draw,
                   const oglSurfaceDescriptor *read)
{
    (void)context;
    installedDraw = *draw;
    installedRead = *read;
    ++updates;
}

static void check_locked_target(eglSurface_s *surface, MockBitmap *bitmap, int op,
                                 int expectedStride)
{
    int previousUpdates = updates;
    CHECK(grLockSurface(op) == 1);
    CHECK(surface->colorBuffer == newPixels);
    CHECK(surface->colorBufferStride == expectedStride);
    CHECK(updates == previousUpdates + 1);
    CHECK(installedDraw.colorBuffer == newPixels && installedRead.colorBuffer == newPixels);
    CHECK(installedDraw.width == bitmap->size.iWidth && installedDraw.height == bitmap->size.iHeight);
    CHECK(installedRead.colorBufferStride == expectedStride);
    CHECK(grReleaseSurface(op) == 1);
    CHECK(bitmap->locks == 0);
}

static void test_buffer_allocation_failure(void)
{
    MockHeap heap;
    eglWindowSurface window;
    eglPixmapSurface invalidPixmap;
    SymTSize windowSize = { 8, 6 };
    int failAt;

    memset(&window, 0, sizeof(window));
    window.surface.type = EGL_SURFACE_WINDOW;
    window.surface.configID = CONFIG_RGBA8_WINDOW_DEPTH16_STENCIL8;
    window.surface.width = windowSize.iWidth;
    window.surface.height = windowSize.iHeight;
    window.nativeWindow = (eglNativeWindow_s *)&windowSize;
    state.heap = &heap;

    /* ARM 0x8B70 attempts both allocations, then frees both on either failure. */
    for (failAt = 1; failAt <= 2; ++failAt)
    {
        memset(&heap, 0, sizeof(heap));
        heap.vtable = &heapVtable;
        heap.failAt = failAt;
        CHECK(eglUpdateSurfaceBuffers(&window.surface) == 0);
        CHECK(heap.calls == 2 && heap.frees == 1);
        CHECK(heap.freed[0] == heap.allocations[2 - failAt]);
        CHECK(window.surface.depthBuffer == NULL && window.surface.stencilBuffer == NULL);
    }

    /* Failure also releases storage retained from an earlier update. */
    memset(&heap, 0, sizeof(heap));
    heap.vtable = &heapVtable;
    heap.failAt = 1;
    window.surface.depthBuffer = heap.allocations[1];
    CHECK(eglUpdateSurfaceBuffers(&window.surface) == 0);
    CHECK(heap.calls == 1 && heap.frees == 1);
    CHECK(heap.freed[0] == heap.allocations[1]);
    CHECK(window.surface.depthBuffer == NULL && window.surface.stencilBuffer == NULL);

    /* Invalid native storage clears dimensions and the previous attachments. */
    memset(&heap, 0, sizeof(heap));
    heap.vtable = &heapVtable;
    memset(&invalidPixmap, 0, sizeof(invalidPixmap));
    invalidPixmap.surface.type = EGL_SURFACE_PIXMAP;
    invalidPixmap.surface.configID = CONFIG_RGBA8_PIXMAP_DEPTH16_STENCIL8;
    invalidPixmap.surface.width = invalidPixmap.surface.height = 4;
    invalidPixmap.surface.depthBuffer = heap.allocations[0];
    invalidPixmap.surface.stencilBuffer = heap.allocations[1];
    CHECK(eglUpdateSurfaceBuffers(&invalidPixmap.surface) == 0);
    CHECK(heap.calls == 0 && heap.frees == 2);
    CHECK(heap.freed[0] == heap.allocations[0] && heap.freed[1] == heap.allocations[1]);
    CHECK(invalidPixmap.surface.width == 0 && invalidPixmap.surface.height == 0);
    CHECK(invalidPixmap.surface.depthBuffer == NULL && invalidPixmap.surface.stencilBuffer == NULL);
    state.heap = NULL;
}

int main(void)
{
    oglContext_s context;
    eglWindowSurface window;
    eglPixmapSurface pixmap;
    SymTSize windowSize = { 8, 6 };
    MockBitmap bitmap = { { 4, 4 }, oldPixels, 0, 0 };

    memset(&state, 0, sizeof(state));
    memset(&context, 0, sizeof(context));
    memset(&window, 0, sizeof(window));
    state.currentESContext = &context;
    state.displayMode = EGL_SYMBIAN_ECOLOR16MA;
    window.surface.type = EGL_SURFACE_WINDOW;
    window.surface.configID = CONFIG_RGBA8_WINDOW;
    window.surface.width = window.surface.height = 4;
    window.surface.colorBuffer = oldPixels;
    window.surface.colorBufferStride = 16;
    window.nativeWindow = (eglNativeWindow_s *)&windowSize;
    window.backBufferBitmap = (eglNativePixmap_s *)&bitmap;
    state.currentDrawSurface = state.currentReadSurface = &window.surface;

    /* ARM 0xAF18 acquires the stride before 0x8B1C refreshes dimensions:
     * the first lock retains the old stride, and the next lock updates it. */
    check_locked_target(&window.surface, &bitmap, GR_SURFACE_DRAW, 16);
    CHECK(bitmap.resizes == 1);
    check_locked_target(&window.surface, &bitmap, GR_SURFACE_DRAW, 32);
    CHECK(window.sizeValidated && window.ownWindow == 0);
    {
        int queries = windowQueries, addresses = bitmapAddresses;
        int modes = displayModeQueries, strides = strideQueries, before = updates, i;
        for (i = 0; i < 8; ++i)
        {
            CHECK(grLockSurfaceState(&state, GR_SURFACE_DRAW) == 1);
            CHECK(grReleaseSurfaceState(&state, GR_SURFACE_DRAW) == 1);
        }
        CHECK(windowQueries == queries && bitmapAddresses == addresses + 8);
        CHECK(displayModeQueries == modes && strideQueries == strides);
        CHECK(updates == before && bitmap.locks == 0);
        bitmap.data = oldPixels;
        CHECK(grLockSurfaceState(&state, GR_SURFACE_DRAW) == 1);
        CHECK(window.surface.colorBuffer == oldPixels && installedDraw.colorBuffer == oldPixels);
        CHECK(grReleaseSurfaceState(&state, GR_SURFACE_DRAW) == 1);
        CHECK(updates == before + 1 && window.ownWindow == 0);
        bitmap.data = newPixels;
        check_locked_target(&window.surface, &bitmap, GR_SURFACE_DRAW, 32);
        egliInvalidateWindowSurface(&window.surface);
        CHECK(!window.sizeValidated);
        queries = windowQueries;
        CHECK(grLockSurfaceState(&state, GR_SURFACE_DRAW) == 1);
        CHECK(grReleaseSurfaceState(&state, GR_SURFACE_DRAW) == 1);
        CHECK(windowQueries == queries + 1 && window.sizeValidated);
    }

    /* The native resize notification itself only refreshes dimensions. */
    windowSize.iWidth = 12;
    CHECK(egliSurfaceResized(&window.surface) == 1);
    CHECK(window.surface.width == 12 && window.surface.height == 6);
    CHECK(!window.sizeValidated);
    CHECK(window.surface.colorBuffer == newPixels);
    CHECK(window.surface.colorBufferStride == 32);
    CHECK(bitmap.resizes == 1 && bitmap.size.iWidth == 8);

    /* Resize failure leaves storage unlocked and reports BAD_ALLOC. */
    windowSize.iWidth = 14;
    resizeFails = 1;
    CHECK(grLockSurface(GR_SURFACE_DRAW) == 0);
    CHECK(state.errorCode == EGL_BAD_ALLOC && bitmap.locks == 0);
    CHECK(bitmap.size.iWidth == 8 && window.surface.width == 12);
    CHECK(window.surface.colorBuffer == newPixels && window.surface.colorBufferStride == 32);
    resizeFails = 0;

    /* An incompatible destination sets the error before any heap lock. */
    window.surface.configID = 1;
    CHECK(egliBlitToPixmap(&window.surface, (eglNativePixmap_s *)&bitmap) == 0);
    CHECK(state.errorCode == EGL_BAD_MATCH && bitmap.locks == 0);

    /* Pixmap storage can move independently before it is locked for reading. */
    memset(&pixmap, 0, sizeof(pixmap));
    pixmap.surface.type = EGL_SURFACE_PIXMAP;
    pixmap.surface.configID = CONFIG_RGBA8_PIXMAP;
    pixmap.surface.width = pixmap.surface.height = 4;
    pixmap.surface.colorBuffer = oldPixels;
    pixmap.surface.colorBufferStride = 16;
    pixmap.nativePixmap = (eglNativePixmap_s *)&bitmap;
    state.currentDrawSurface = state.currentReadSurface = &pixmap.surface;
    check_locked_target(&pixmap.surface, &bitmap, GR_SURFACE_READ, 16);
    check_locked_target(&pixmap.surface, &bitmap, GR_SURFACE_READ, 32);
    CHECK(bitmap.resizes == 1);

    {
        int before = tlsCalls;
        CHECK(grLockSurfaceState(&state, GR_SURFACE_READ) == 1);
        CHECK(bitmap.locks == 1);
        CHECK(grReleaseSurfaceState(&state, GR_SURFACE_READ) == 1);
        CHECK(bitmap.locks == 0);
        CHECK(tlsCalls == before);
        CHECK(grLockSurfaceState(NULL, GR_SURFACE_DRAW) == 0);
        CHECK(grReleaseSurfaceState(NULL, GR_SURFACE_DRAW) == 0);
    }

    test_buffer_allocation_failure();

    printf("symbian surface tests: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
