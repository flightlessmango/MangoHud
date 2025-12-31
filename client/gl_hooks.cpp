#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <array>
#include <cstring>
#include <cstdio>
#include "elfhacks.h"
#include "real_dlsym.h"
#include "gl.h"
#include "mesa/os_time.h"
#include <GL/glx.h>
#include <GL/glxext.h>

std::unique_ptr<OverlayGL> overlay;

EXPORT_C_(EGLBoolean)eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    static EGLBoolean (*real_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
    if (!real_eglSwapBuffers)
        real_eglSwapBuffers = (decltype(real_eglSwapBuffers)) real_dlsym(RTLD_NEXT, "eglSwapBuffers");

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
        if (!overlay) overlay = std::make_unique<OverlayGL>();
        overlay->ipc->add_to_queue(os_time_get_nano());
        overlay->draw();
    }

    return real_eglSwapBuffers(dpy, surf);
}

EXPORT_C_(EGLBoolean) eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surf, const EGLint* rects, EGLint n_rects) {    fprintf(stderr, "WITH DAMAGE KHR\n");
    static EGLBoolean (*real_eglSwapBuffersWithDamageKHR)(EGLDisplay, EGLSurface, const EGLint*, EGLint) = nullptr;
    if (!real_eglSwapBuffersWithDamageKHR) {
        real_eglSwapBuffersWithDamageKHR =
            (decltype(real_eglSwapBuffersWithDamageKHR))
            real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageKHR");
    }

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
        if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
            if (!overlay) overlay = std::make_unique<OverlayGL>();
            overlay->ipc->add_to_queue(os_time_get_nano());
            overlay->draw();
        }
    }

    return real_eglSwapBuffersWithDamageKHR
        ? real_eglSwapBuffersWithDamageKHR(dpy, surf, rects, n_rects)
        : EGL_FALSE;
}

EXPORT_C_(EGLBoolean) eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surf, const EGLint* rects, EGLint n_rects) {
    static EGLBoolean (*real_eglSwapBuffersWithDamageEXT)(EGLDisplay, EGLSurface, const EGLint*, EGLint) = nullptr;
    if (!real_eglSwapBuffersWithDamageEXT) {
        real_eglSwapBuffersWithDamageEXT =
            (decltype(real_eglSwapBuffersWithDamageEXT))
            real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageEXT");
    }

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
        if (eglGetCurrentContext() != EGL_NO_CONTEXT) {
            if (!overlay) overlay = std::make_unique<OverlayGL>();
            overlay->ipc->add_to_queue(os_time_get_nano());
            overlay->draw();
        }
    }

    return real_eglSwapBuffersWithDamageEXT
        ? real_eglSwapBuffersWithDamageEXT(dpy, surf, rects, n_rects)
        : EGL_FALSE;
}

EXPORT_C_(void) glXSwapBuffers(Display* dpy, GLXDrawable drawable) {
    static void (*real_glXSwapBuffers)(Display*, GLXDrawable) = nullptr;
    if (!real_glXSwapBuffers)
        real_glXSwapBuffers = (void (*)(Display*, GLXDrawable))real_dlsym(RTLD_NEXT, "glXSwapBuffers");

    if (!dpy || drawable == 0)
        return real_glXSwapBuffers(dpy, drawable);

    if (!overlay)
        overlay = std::make_unique<OverlayGL>(dpy);

    overlay->xdpy = dpy;
    overlay->draw();
    overlay->ipc->add_to_queue(os_time_get_nano());
    return real_glXSwapBuffers(dpy, drawable);
}

extern "C" int64_t glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder);
EXPORT_C_(int64_t) glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    static int64_t (*real_glXSwapBuffersMscOML)(Display*, GLXDrawable, int64_t, int64_t, int64_t) = nullptr;
    if (!real_glXSwapBuffersMscOML)
        real_glXSwapBuffersMscOML = (int64_t (*)(Display*, GLXDrawable, int64_t, int64_t, int64_t))real_dlsym(RTLD_NEXT, "glXSwapBuffersMscOML");

    if (!overlay)
        overlay = std::make_unique<OverlayGL>(dpy);

    overlay->xdpy = dpy;
    overlay->ipc->add_to_queue(os_time_get_nano());
    overlay->draw();

    return real_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
}

struct func_ptr {
    const char* name;
    void* ptr;
};

static const auto name_to_funcptr_map = std::array{
#define ADD_HOOK(fn) func_ptr{ #fn, (void*)fn }
    ADD_HOOK(eglSwapBuffers),
    ADD_HOOK(glXSwapBuffers),
    ADD_HOOK(glXSwapBuffersMscOML),
    ADD_HOOK(eglSwapBuffersWithDamageKHR),
    ADD_HOOK(eglSwapBuffersWithDamageEXT),
#undef ADD_HOOK
};

extern "C" void* dlsym(void* handle, const char* symbol)
{
    for (const auto& f : name_to_funcptr_map)
        if (std::strcmp(symbol, f.name) == 0) return f.ptr;

    return real_dlsym(handle, symbol);
}
