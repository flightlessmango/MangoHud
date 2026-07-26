#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <wayland-egl-backend.h>
#include <wayland-egl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include "elfhacks.h"
#include "real_dlsym.h"
#include "gl.h"
#include "mesa/os_time.h"
#include <GL/glx.h>
#include <GL/glxext.h>
#include "wayland.h"

std::unique_ptr<OverlayGL> overlay;
std::shared_ptr<IPCClient> ipc;
std::unique_ptr<Wayland> wayland;
std::mutex wl_egl_windows_m;
std::unordered_map<wl_egl_window*, wl_surface*> wl_egl_windows;
std::mutex egl_displays_m;
std::unordered_map<EGLDisplay, wl_display*> egl_displays;

static wl_surface* get_wl_egl_surface(wl_egl_window* window) {
    if (!window)
        return nullptr;

    {
        std::lock_guard lock(wl_egl_windows_m);
        auto it = wl_egl_windows.find(window);
        if (it != wl_egl_windows.end())
            return it->second;
    }

    if (window->version == WL_EGL_WINDOW_VERSION)
        return window->surface;

    return reinterpret_cast<wl_surface*>(window->version);
}

static wl_display* get_egl_display(EGLDisplay dpy) {
    std::lock_guard lock(egl_displays_m);
    auto it = egl_displays.find(dpy);
    return it != egl_displays.end() ? it->second : nullptr;
}

static wl_display* remove_egl_display(EGLDisplay dpy) {
    std::lock_guard lock(egl_displays_m);
    auto it = egl_displays.find(dpy);
    if (it == egl_displays.end())
        return nullptr;

    auto* display = it->second;
    egl_displays.erase(it);
    return display;
}

static void add_egl_display(EGLDisplay dpy, void* native_display) {
    if (dpy == EGL_NO_DISPLAY || !native_display)
        return;

    std::lock_guard lock(egl_displays_m);
    egl_displays[dpy] = static_cast<wl_display*>(native_display);
}

static void register_egl_surface(EGLDisplay dpy, EGLSurface surf, void* native_window) {
    if (surf == EGL_NO_SURFACE || !native_window)
        return;

    auto* wl_surface = get_wl_egl_surface(reinterpret_cast<wl_egl_window*>(native_window));
    auto* wl_display = get_egl_display(dpy);
    if (!wl_surface || !wl_display)
        return;

    add_egl_display(dpy, wl_display);

    if (!ipc) ipc = std::make_shared<IPCClient>(nullptr, Backend::EGL);
    if (!wayland)
        wayland = std::make_unique<Wayland>(ipc);
    wayland->add_surface(surf, wl_surface, wl_display);
}

static bool present_wayland(EGLSurface surf) {
    if (!wayland)
        return false;

    ipc->add_to_queue(os_time_get_nano());
    return wayland->ensure_overlay(surf);
}

static void mangohud(Display *dpy = nullptr) {
    auto api = dpy ? Backend::GLX : Backend::EGL;
    if (!ipc) ipc = std::make_shared<IPCClient>(nullptr, api);
    if (!overlay) overlay = std::make_unique<OverlayGL>(nullptr, ipc);
    if (dpy) overlay->xdpy = dpy;
    overlay->ipc->add_to_queue(os_time_get_nano());
    overlay->draw();
}

EXPORT_C_(EGLBoolean)eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    static EGLBoolean (*real_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
    if (!real_eglSwapBuffers)
        real_eglSwapBuffers = (decltype(real_eglSwapBuffers)) real_dlsym(RTLD_NEXT, "eglSwapBuffers");

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE)
        if (!present_wayland(surf))
            mangohud();

    return real_eglSwapBuffers(dpy, surf);
}

EXPORT_C_(EGLDisplay) eglGetPlatformDisplay(EGLenum platform, void* native_display, const EGLAttrib* attrib_list) {
    static EGLDisplay (*real_eglGetPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = nullptr;
    if (!real_eglGetPlatformDisplay)
        real_eglGetPlatformDisplay = (decltype(real_eglGetPlatformDisplay)) real_dlsym(RTLD_NEXT, "eglGetPlatformDisplay");

    EGLDisplay dpy = real_eglGetPlatformDisplay(platform, native_display, attrib_list);
    if (platform == EGL_PLATFORM_WAYLAND_KHR)
        add_egl_display(dpy, native_display);

    return dpy;
}

EXPORT_C_(EGLDisplay) eglGetPlatformDisplayEXT(EGLenum platform, void* native_display, const EGLint* attrib_list) {
    static EGLDisplay (*real_eglGetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
    if (!real_eglGetPlatformDisplayEXT)
        real_eglGetPlatformDisplayEXT = (decltype(real_eglGetPlatformDisplayEXT)) real_dlsym(RTLD_NEXT, "eglGetPlatformDisplayEXT");

    EGLDisplay dpy = real_eglGetPlatformDisplayEXT(platform, native_display, attrib_list);
    if (platform == EGL_PLATFORM_WAYLAND_KHR)
        add_egl_display(dpy, native_display);

    return dpy;
}

EXPORT_C_(EGLDisplay) eglGetDisplay(EGLNativeDisplayType native_display) {
    static EGLDisplay (*real_eglGetDisplay)(EGLNativeDisplayType) = nullptr;
    if (!real_eglGetDisplay)
        real_eglGetDisplay = (decltype(real_eglGetDisplay)) real_dlsym(RTLD_NEXT, "eglGetDisplay");

    EGLDisplay dpy = real_eglGetDisplay(native_display);
    add_egl_display(dpy, reinterpret_cast<void*>(native_display));

    return dpy;
}

EXPORT_C_(EGLSurface) eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                             EGLNativeWindowType native_window,
                                             const EGLint* attrib_list) {
    static EGLSurface (*real_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) = nullptr;
    if (!real_eglCreateWindowSurface)
        real_eglCreateWindowSurface = (decltype(real_eglCreateWindowSurface)) real_dlsym(RTLD_NEXT, "eglCreateWindowSurface");

    EGLSurface surf = real_eglCreateWindowSurface(dpy, config, native_window, attrib_list);
    register_egl_surface(dpy, surf, reinterpret_cast<void*>(native_window));

    return surf;
}

EXPORT_C_(EGLSurface) eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     void* native_window,
                                                     const EGLAttrib* attrib_list) {
    static EGLSurface (*real_eglCreatePlatformWindowSurface)(EGLDisplay, EGLConfig, void*, const EGLAttrib*) = nullptr;
    if (!real_eglCreatePlatformWindowSurface)
        real_eglCreatePlatformWindowSurface =
            (decltype(real_eglCreatePlatformWindowSurface)) real_dlsym(RTLD_NEXT, "eglCreatePlatformWindowSurface");

    EGLSurface surf = real_eglCreatePlatformWindowSurface
        ? real_eglCreatePlatformWindowSurface(dpy, config, native_window, attrib_list)
        : EGL_NO_SURFACE;
    register_egl_surface(dpy, surf, native_window);

    return surf;
}

EXPORT_C_(EGLSurface) eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                                        void* native_window,
                                                        const EGLint* attrib_list) {
    static EGLSurface (*real_eglCreatePlatformWindowSurfaceEXT)(EGLDisplay, EGLConfig, void*, const EGLint*) = nullptr;
    if (!real_eglCreatePlatformWindowSurfaceEXT)
        real_eglCreatePlatformWindowSurfaceEXT =
            (decltype(real_eglCreatePlatformWindowSurfaceEXT)) real_dlsym(RTLD_NEXT, "eglCreatePlatformWindowSurfaceEXT");

    EGLSurface surf = real_eglCreatePlatformWindowSurfaceEXT
        ? real_eglCreatePlatformWindowSurfaceEXT(dpy, config, native_window, attrib_list)
        : EGL_NO_SURFACE;
    register_egl_surface(dpy, surf, native_window);

    return surf;
}

EXPORT_C_(EGLBoolean) eglDestroySurface(EGLDisplay dpy, EGLSurface surf) {
    static EGLBoolean (*real_eglDestroySurface)(EGLDisplay, EGLSurface) = nullptr;
    if (!real_eglDestroySurface)
        real_eglDestroySurface = (decltype(real_eglDestroySurface)) real_dlsym(RTLD_NEXT, "eglDestroySurface");

    if (wayland)
        wayland->destroy_surface(surf);

    return real_eglDestroySurface(dpy, surf);
}

EXPORT_C_(EGLBoolean) eglTerminate(EGLDisplay dpy) {
    static EGLBoolean (*real_eglTerminate)(EGLDisplay) = nullptr;
    if (!real_eglTerminate)
        real_eglTerminate = (decltype(real_eglTerminate)) real_dlsym(RTLD_NEXT, "eglTerminate");

    auto* display = remove_egl_display(dpy);
    if (wayland && display) {
        wayland->destroy_egl_display_surfaces(display);
        wayland.reset();
    }

    return real_eglTerminate(dpy);
}

EXPORT_C_(wl_egl_window*) wl_egl_window_create(wl_surface* surface, int width, int height) {
    static wl_egl_window* (*real_wl_egl_window_create)(wl_surface*, int, int) = nullptr;
    if (!real_wl_egl_window_create)
        real_wl_egl_window_create = (decltype(real_wl_egl_window_create)) real_dlsym(RTLD_NEXT, "wl_egl_window_create");

    auto* window = real_wl_egl_window_create(surface, width, height);

    if (window && surface) {
        std::lock_guard lock(wl_egl_windows_m);
        wl_egl_windows[window] = surface;
    }

    return window;
}

EXPORT_C_(void) wl_egl_window_destroy(wl_egl_window* window) {
    static void (*real_wl_egl_window_destroy)(wl_egl_window*) = nullptr;
    if (!real_wl_egl_window_destroy)
        real_wl_egl_window_destroy = (decltype(real_wl_egl_window_destroy)) real_dlsym(RTLD_NEXT, "wl_egl_window_destroy");

    {
        std::lock_guard lock(wl_egl_windows_m);
        wl_egl_windows.erase(window);
    }

    wayland.reset();
    real_wl_egl_window_destroy(window);
}

EXPORT_C_(void) wl_display_disconnect(wl_display* display) {
    static void (*real_wl_display_disconnect)(wl_display*) = nullptr;
    if (!real_wl_display_disconnect)
        real_wl_display_disconnect = (decltype(real_wl_display_disconnect)) real_dlsym(RTLD_NEXT, "wl_display_disconnect");

    wayland.reset();
    real_wl_display_disconnect(display);
}

EXPORT_C_(EGLBoolean) eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surf, const EGLint* rects, EGLint n_rects) {
    static EGLBoolean (*real_eglSwapBuffersWithDamageKHR)(EGLDisplay, EGLSurface, const EGLint*, EGLint) = nullptr;
    if (!real_eglSwapBuffersWithDamageKHR) {
        real_eglSwapBuffersWithDamageKHR =
            (decltype(real_eglSwapBuffersWithDamageKHR))
            real_dlsym(RTLD_NEXT, "eglSwapBuffersWithDamageKHR");
    }

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE)
        if (eglGetCurrentContext() != EGL_NO_CONTEXT)
            if (!present_wayland(surf))
                mangohud();

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

    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE)
        if (eglGetCurrentContext() != EGL_NO_CONTEXT)
            if (!present_wayland(surf))
                mangohud();

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

    mangohud(dpy);

    return real_glXSwapBuffers(dpy, drawable);
}

extern "C" int64_t glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder);
EXPORT_C_(int64_t) glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    static int64_t (*real_glXSwapBuffersMscOML)(Display*, GLXDrawable, int64_t, int64_t, int64_t) = nullptr;
    if (!real_glXSwapBuffersMscOML)
        real_glXSwapBuffersMscOML = (int64_t (*)(Display*, GLXDrawable, int64_t, int64_t, int64_t))real_dlsym(RTLD_NEXT, "glXSwapBuffersMscOML");

    mangohud(dpy);

    return real_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
}

struct func_ptr {
    const char* name;
    void* ptr;
};

EXPORT_C_(__eglMustCastToProperFunctionPointerType) eglGetProcAddress(const char* procName);

static const auto name_to_funcptr_map = std::array{
#define ADD_HOOK(fn) func_ptr{ #fn, (void*)fn }
    ADD_HOOK(eglGetProcAddress),
    ADD_HOOK(eglSwapBuffers),
    ADD_HOOK(glXSwapBuffers),
    ADD_HOOK(glXSwapBuffersMscOML),
    ADD_HOOK(eglSwapBuffersWithDamageKHR),
    ADD_HOOK(eglSwapBuffersWithDamageEXT),
    ADD_HOOK(eglGetPlatformDisplay),
    ADD_HOOK(eglGetPlatformDisplayEXT),
    ADD_HOOK(eglGetDisplay),
    ADD_HOOK(eglCreateWindowSurface),
    ADD_HOOK(eglCreatePlatformWindowSurface),
    ADD_HOOK(eglCreatePlatformWindowSurfaceEXT),
    ADD_HOOK(eglDestroySurface),
    ADD_HOOK(eglTerminate),
    ADD_HOOK(wl_egl_window_create),
    ADD_HOOK(wl_egl_window_destroy),
    ADD_HOOK(wl_display_disconnect),
#undef ADD_HOOK
};

static void* find_hook(const char* name)
{
    for (const auto& f : name_to_funcptr_map)
        if (std::strcmp(name, f.name) == 0) return f.ptr;

    return nullptr;
}

EXPORT_C_(__eglMustCastToProperFunctionPointerType) eglGetProcAddress(const char* procName)
{
    static __eglMustCastToProperFunctionPointerType (*real_eglGetProcAddress)(const char*) = nullptr;
    if (!real_eglGetProcAddress)
        real_eglGetProcAddress =
            (decltype(real_eglGetProcAddress)) real_dlsym(RTLD_NEXT, "eglGetProcAddress");

    auto real_func = real_eglGetProcAddress ? real_eglGetProcAddress(procName) : nullptr;
    if (real_func) {
        auto* func = find_hook(procName);
        if (func)
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(func);
    }

    return real_func;
}

extern "C" void* dlsym(void* handle, const char* symbol)
{
    auto* func = find_hook(symbol);
    if (func)
        return func;

    return real_dlsym(handle, symbol);
}
