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
#include "imgui_hud.h"

using namespace MangoHud::GL;

EXPORT_C_(void *) eglGetProcAddress(const char* procName);

void* get_egl_proc_address(const char* name) {

    void *func = nullptr;
    static void *(*pfn_eglGetProcAddress)(const char*) = nullptr;
    if (!pfn_eglGetProcAddress) {
        void *handle = real_dlopen("libEGL.so.1", RTLD_LAZY|RTLD_LOCAL);
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
        SPDLOG_DEBUG("Failed to get function '{}'", name);
    }

    return func;
}

//EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EXPORT_C_(int) eglMakeCurrent_OFF(void *dpy, void *draw, void *read,void *ctx) {
    SPDLOG_TRACE("{}: draw: {}, ctx: {}", __func__, draw, ctx);
    int ret = 0;
    return ret;
}

EXPORT_C_(unsigned int) eglSwapBuffers( void* dpy, void* surf)
{
    static int (*pfn_eglSwapBuffers)(void*, void*) = nullptr;
    if (!pfn_eglSwapBuffers)
        pfn_eglSwapBuffers = reinterpret_cast<decltype(pfn_eglSwapBuffers)>(get_egl_proc_address("eglSwapBuffers"));

    if (!is_blacklisted()) {
        static int (*pfn_eglQuerySurface)(void* dpy, void* surface, int attribute, int *value) = nullptr;
        if (!pfn_eglQuerySurface)
            pfn_eglQuerySurface = reinterpret_cast<decltype(pfn_eglQuerySurface)>(get_egl_proc_address("eglQuerySurface"));

        imgui_create(surf, gl_platform::EGL);

        int width=0, height=0;
        if (pfn_eglQuerySurface(dpy, surf, 0x3056, &height) &&
            pfn_eglQuerySurface(dpy, surf, 0x3057, &width))
            imgui_render(width, height);

        using namespace std::chrono_literals;
        if (fps_limit_stats.targetFrameTime > 0s){
            fps_limit_stats.frameStart = Clock::now();
            FpsLimiter(fps_limit_stats);
            fps_limit_stats.frameEnd = Clock::now();
        }
    }

    return pfn_eglSwapBuffers(dpy, surf);
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 2> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(eglGetProcAddress),
   ADD_HOOK(eglSwapBuffers),
#undef ADD_HOOK
}};

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
