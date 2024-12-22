#include <iostream>
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <chrono>
#include <iomanip>
#include <spdlog/spdlog.h>
#include "real_dlsym.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"
#include "blacklist.h"
#include "gl_hud.h"
#ifdef HAVE_WAYLAND
#include "wayland_hook.h"
#endif

using namespace MangoHud::GL;

#define EGL_PLATFORM_WAYLAND_KHR          0x31D8

EXPORT_C_(void *) eglGetProcAddress(const char* procName);

static void* get_egl_proc_address(const char* name) {

    void *func = nullptr;
    static void *(*pfn_eglGetProcAddress)(const char*) = nullptr;
    if (!pfn_eglGetProcAddress) {
        void *handle = real_dlopen("libEGL.so.1", RTLD_LAZY);
        if (!handle) {
            SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " libEGL.so.1: {}", dlerror());
        } else {
            pfn_eglGetProcAddress = reinterpret_cast<decltype(pfn_eglGetProcAddress)>(real_dlsym(handle, "eglGetProcAddress"));
        }
    }

    if (pfn_eglGetProcAddress)
        func = pfn_eglGetProcAddress(name);

    if (!func)
        func = get_proc_address( name );

    if (!func) {
        SPDLOG_ERROR("Failed to get function '{}'", name);
    }

    return func;
}

EXPORT_C_(unsigned int) eglSwapBuffers( void* dpy, void* surf);
EXPORT_C_(unsigned int) eglSwapBuffers( void* dpy, void* surf)
{
    static int (*pfn_eglSwapBuffers)(void*, void*) = nullptr;
    if (!pfn_eglSwapBuffers)
        pfn_eglSwapBuffers = reinterpret_cast<decltype(pfn_eglSwapBuffers)>(get_egl_proc_address("eglSwapBuffers"));

    if (!is_blacklisted()) {
        static int (*pfn_eglQuerySurface)(void* dpy, void* surface, int attribute, int *value) = nullptr;
        if (!pfn_eglQuerySurface)
            pfn_eglQuerySurface = reinterpret_cast<decltype(pfn_eglQuerySurface)>(get_egl_proc_address("eglQuerySurface"));

        imgui_create(surf, gl_wsi::GL_WSI_EGL);

        int width=0, height=0;
        if (pfn_eglQuerySurface(dpy, surf, 0x3056, &height) &&
            pfn_eglQuerySurface(dpy, surf, 0x3057, &width))
            imgui_render(width, height);

        using namespace std::chrono_literals;
        if (fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_EARLY){
            fps_limit_stats.frameStart = Clock::now();
            FpsLimiter(fps_limit_stats);
            fps_limit_stats.frameEnd = Clock::now();
        }
    }

    int res = pfn_eglSwapBuffers(dpy, surf);

    if (!is_blacklisted()) {
        using namespace std::chrono_literals;
        if (fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_LATE){
            fps_limit_stats.frameStart = Clock::now();
            FpsLimiter(fps_limit_stats);
            fps_limit_stats.frameEnd = Clock::now();
        }
    }

    return res;
}

EXPORT_C_(void*) eglGetPlatformDisplay( unsigned int platform, void* native_display, const intptr_t* attrib_list);
EXPORT_C_(void*) eglGetPlatformDisplay( unsigned int platform, void* native_display, const intptr_t* attrib_list)
{
    static void* (*pfn_eglGetPlatformDisplay)(unsigned int, void*, const intptr_t*) = nullptr;
    if (!pfn_eglGetPlatformDisplay)
        pfn_eglGetPlatformDisplay = reinterpret_cast<decltype(pfn_eglGetPlatformDisplay)>(get_egl_proc_address("eglGetPlatformDisplay"));

#ifdef HAVE_WAYLAND
    if(platform == EGL_PLATFORM_WAYLAND_KHR)
    {
        wl_display* display = static_cast<wl_display*>(native_display);
        HUDElements.display_server = HUDElements.display_servers::WAYLAND;
        init_wayland_data(display);
    }
#endif

    return pfn_eglGetPlatformDisplay(platform, native_display, attrib_list);
}

EXPORT_C_(void*) eglGetDisplay( void* native_display );
EXPORT_C_(void*) eglGetDisplay( void* native_display )
{
    static void* (*pfn_eglGetDisplay)(void*) = nullptr;
    if (!pfn_eglGetDisplay)
        pfn_eglGetDisplay = reinterpret_cast<decltype(pfn_eglGetDisplay)>(get_egl_proc_address("eglGetDisplay"));

#ifdef HAVE_WAYLAND
    try
    {
        wl_interface* iface = *static_cast<wl_interface**>(native_display);
        if(iface && strcmp(iface->name, wl_display_interface.name) == 0)
        {
            wl_display* display = static_cast<wl_display*>(native_display);
            HUDElements.display_server = HUDElements.display_servers::WAYLAND;
            init_wayland_data(display);
        }
    }
    catch(...)
    {
    }
#endif

    return pfn_eglGetDisplay(native_display);
}

EXPORT_C_(unsigned) eglTerminate( void* native_display );
EXPORT_C_(unsigned) eglTerminate( void* native_display )
{
    static unsigned (*pfn_eglTerminate)(void*) = nullptr;
    if (!pfn_eglTerminate)
        pfn_eglTerminate= reinterpret_cast<decltype(pfn_eglTerminate)>(get_egl_proc_address("eglTerminate"));

#ifdef HAVE_WAYLAND
    fini_wayland_data();
#endif

    return pfn_eglTerminate(native_display);
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 5> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(eglGetProcAddress),
   ADD_HOOK(eglSwapBuffers),
   ADD_HOOK(eglGetPlatformDisplay),
   ADD_HOOK(eglGetDisplay),
   ADD_HOOK(eglTerminate)
#undef ADD_HOOK
}};

EXPORT_C_(void *) mangohud_find_egl_ptr(const char *name);
EXPORT_C_(void *) mangohud_find_egl_ptr(const char *name)
{
  if (is_blacklisted())
      return nullptr;

   for (auto& func : name_to_funcptr_map) {
      if (strcmp(name, func.name) == 0)
         return func.ptr;
   }

   return nullptr;
}

EXPORT_C_(void *) eglGetProcAddress(const char* procName) {
    void* real_func = get_egl_proc_address(procName);
    void* func = mangohud_find_egl_ptr(procName);
    SPDLOG_TRACE("{}: proc: {}, real: {}, fun: {}", __func__, procName, real_func, func);
    if (func && real_func)
        return func;

    return real_func;
}
