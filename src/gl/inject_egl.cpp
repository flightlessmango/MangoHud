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
#include "wsi_helpers.h"

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-egl.h>
#endif

#include <sys/mman.h>
#include <unistd.h>
#define HAVE_MINCORE

using namespace MangoHud::GL;

namespace MangoHud { namespace GL {
   extern swapchain_stats sw_stats;
   extern wsi_connection wsi_conn;
}}

EXPORT_C_(void*) eglGetProcAddress(const char* procName);
static GL_SESSION egl_session = GL_SESSION_UNKNOWN;

// mesa eglglobals.c
static bool pointer_is_dereferencable(void *p)
{
#ifdef HAVE_MINCORE
   uintptr_t addr = (uintptr_t) p;
   unsigned char valid = 0;
   const long page_size = getpagesize();

   if (p == NULL)
      return false;

   /* align addr to page_size */
   addr &= ~(page_size - 1);

   if (mincore((void *) addr, page_size, &valid) < 0) {
      return false;
   }

   /* mincore() returns 0 on success, and -1 on failure.  The last parameter
    * is a vector of bytes with one entry for each page queried.  mincore
    * returns page residency information in the first bit of each byte in the
    * vector.
    *
    * Residency doesn't actually matter when determining whether a pointer is
    * dereferenceable, so the output vector can be ignored.  What matters is
    * whether mincore succeeds. See:
    *
    *   http://man7.org/linux/man-pages/man2/mincore.2.html
    */
   return true;
#else
   return p != NULL;
#endif
}

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

EXPORT_C_(void*) eglGetDisplay(void* native_display)
{
    static decltype(&eglGetDisplay) pfn_eglGetDisplay = nullptr;
    if (!pfn_eglGetDisplay)
        pfn_eglGetDisplay = reinterpret_cast<decltype(pfn_eglGetDisplay)>(get_egl_proc_address("eglGetDisplay"));

    if (getenv("WAYLAND_DISPLAY"))
    {
        egl_session = GL_SESSION_WL;
        wsi_conn.wl.display = reinterpret_cast<wl_display*> (native_display);
    }
    else if (getenv("DISPLAY"))
    {
        egl_session = GL_SESSION_X11;
        wsi_conn.xlib.dpy = reinterpret_cast<Display*> (native_display);
    }

    if (pointer_is_dereferencable(native_display))
    {
        Dl_info info;
        void *first_pointer = *(void **) native_display;
        dladdr(first_pointer, &info);
        if (info.dli_saddr)
            fprintf(stderr, "dli_sname: %s\n", info.dli_sname);
    }

    return pfn_eglGetDisplay(native_display);
}

EXPORT_C_(void*) eglCreateWindowSurface(void *egl_display, void *egl_config, void *native_window, int const * attrib_list)
{
    static decltype(&eglCreateWindowSurface) pfn_eglCreateWindowSurface = nullptr;
    if (!pfn_eglCreateWindowSurface)
        pfn_eglCreateWindowSurface = reinterpret_cast<decltype(pfn_eglCreateWindowSurface)>(get_egl_proc_address("eglCreateWindowSurface"));

    if (egl_session == GL_SESSION_WL)
    {
//         wsi_conn.wl.surface = reinterpret_cast<wl_surface*>(native_window);
    }
    else if (egl_session == GL_SESSION_X11)
    {
        wsi_conn.xlib.window = reinterpret_cast<Window>(native_window);
    }
    return pfn_eglCreateWindowSurface(egl_display, egl_config, native_window, attrib_list);
}

EXPORT_C_(wl_egl_window*) wl_egl_window_create(wl_surface *surf, int w, int h)
{
    static decltype(&wl_egl_window_create) pfn_wl_egl_window_create = nullptr;
    if (!pfn_wl_egl_window_create)
    {
        void *hlib = dlopen("libwayland-egl.so.1", RTLD_LAZY);
        pfn_wl_egl_window_create = reinterpret_cast<decltype(pfn_wl_egl_window_create)>(dlsym(hlib, "wl_egl_window_create"));
    }

    fprintf(stderr, "%s\n", __func__);
    wsi_conn.wl.surface = surf;
    return pfn_wl_egl_window_create(surf, w, h);
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

        imgui_create(surf, egl_session);

        int width=0, height=0;
        if (pfn_eglQuerySurface(dpy, surf, 0x3056, &height) &&
            pfn_eglQuerySurface(dpy, surf, 0x3057, &width))
            imgui_render(width, height);

        using namespace std::chrono_literals;
        if (fps_limit_stats.targetFrameTime > 0s || (sw_stats.lost_focus && fps_limit_stats.focusLossFrameTime > 0s)){
            fps_limit_stats.frameStart = Clock::now();
            FpsLimiter(fps_limit_stats, sw_stats.lost_focus);
            fps_limit_stats.frameEnd = Clock::now();
        }
    }

    return pfn_eglSwapBuffers(dpy, surf);
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 4> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(eglGetProcAddress),
   ADD_HOOK(eglSwapBuffers),
   ADD_HOOK(eglGetDisplay),
   ADD_HOOK(wl_egl_window_create),
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

void* eglGetProcAddress(const char* procName)
{
    void* real_func = get_egl_proc_address(procName);
    void* func = mangohud_find_egl_ptr(procName);
    SPDLOG_TRACE("{}: proc: {}, real: {}, fun: {}", __func__, procName, real_func, func);
    if (func && real_func)
        return func;

    return real_func;
}
