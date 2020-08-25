#include <X11/Xlib.h>
#include <iostream>
#include <array>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>
#include "real_dlsym.h"
#include "loaders/loader_glx.h"
#include "loaders/loader_x11.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"
#include "blacklist.h"

#include <chrono>
#include <iomanip>

#include "imgui_hud.h"

using namespace MangoHud::GL;

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName);
EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName);

#ifndef GLX_WIDTH
#define GLX_WIDTH   0x801D
#define GLX_HEIGTH  0x801E
#endif

static glx_loader glx;

static std::vector<std::thread::id> gl_threads;

void* get_glx_proc_address(const char* name) {
    glx.Load();

    void *func = nullptr;
    if (glx.GetProcAddress)
        func = glx.GetProcAddress( (const unsigned char*) name );

    if (!func && glx.GetProcAddressARB)
        func = glx.GetProcAddressARB( (const unsigned char*) name );

    if (!func)
        func = get_proc_address( name );

    if (!func) {
        std::cerr << "MANGOHUD: Failed to get function '" << name << "'" << std::endl;
    }

    return func;
}

EXPORT_C_(void *) glXCreateContext(void *dpy, void *vis, void *shareList, int direct)
{
    glx.Load();
    void *ctx = glx.CreateContext(dpy, vis, shareList, direct);
#ifndef NDEBUG
    std::cerr << __func__ << ":" << ctx << std::endl;
#endif
    return ctx;
}

EXPORT_C_(int) glXMakeCurrent(void* dpy, void* drawable, void* ctx) {
    glx.Load();
#ifndef NDEBUG
    std::cerr << __func__ << ": " << drawable << ", " << ctx << std::endl;
#endif

    int ret = glx.MakeCurrent(dpy, drawable, ctx);

    if (!is_blacklisted()) {
        if (ret) {
            //TODO might as well just ignore everything here as long as VBOs get recreated anyway
            auto it = std::find(gl_threads.begin(), gl_threads.end(), std::this_thread::get_id());
            if (!ctx) {
                if (it != gl_threads.end())
                    gl_threads.erase(it);
                if (!gl_threads.size())
                    imgui_set_context(nullptr);
            } else {
                if (it == gl_threads.end())
                    gl_threads.push_back(std::this_thread::get_id());
                imgui_set_context(ctx);
#ifndef NDEBUG
                std::cerr << "MANGOHUD: GL thread count: " << gl_threads.size() << "\n";
#endif
            }
        }

        if (params.gl_vsync >= -1) {
            if (glx.SwapIntervalEXT)
                glx.SwapIntervalEXT(dpy, drawable, params.gl_vsync);
            if (glx.SwapIntervalSGI)
                glx.SwapIntervalSGI(params.gl_vsync);
            if (glx.SwapIntervalMESA)
                glx.SwapIntervalMESA(params.gl_vsync);
        }
    }

    return ret;
}

static void do_imgui_swap(void *dpy, void *drawable)
{
    if (!is_blacklisted()) {
        imgui_create(glx.GetCurrentContext());

        unsigned int width = -1, height = -1;

        glx.QueryDrawable(dpy, drawable, GLX_WIDTH, &width);
        glx.QueryDrawable(dpy, drawable, GLX_HEIGTH, &height);

        /*GLint vp[4]; glGetIntegerv (GL_VIEWPORT, vp);
        width = vp[2];
        height = vp[3];*/

        imgui_render(width, height);
    }
}

EXPORT_C_(void) glXSwapBuffers(void* dpy, void* drawable) {
    glx.Load();

    do_imgui_swap(dpy, drawable);
    glx.SwapBuffers(dpy, drawable);

    using namespace std::chrono_literals;
    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s){
        fps_limit_stats.frameStart = Clock::now();
        FpsLimiter(fps_limit_stats);
        fps_limit_stats.frameEnd = Clock::now();
    }
}

EXPORT_C_(int64_t) glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder)
{
    glx.Load();

    do_imgui_swap(dpy, drawable);
    int64_t ret = glx.SwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);

    using namespace std::chrono_literals;
    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s){
        fps_limit_stats.frameStart = Clock::now();
        FpsLimiter(fps_limit_stats);
        fps_limit_stats.frameEnd = Clock::now();
    }
    return ret;
}

EXPORT_C_(void) glXSwapIntervalEXT(void *dpy, void *draw, int interval) {
#ifndef NDEBUG
    std::cerr << __func__ << ": " << interval << std::endl;
#endif
    glx.Load();

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = params.gl_vsync;

    glx.SwapIntervalEXT(dpy, draw, interval);
}

EXPORT_C_(int) glXSwapIntervalSGI(int interval) {
#ifndef NDEBUG
    std::cerr << __func__ << ": " << interval << std::endl;
#endif
    glx.Load();

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = params.gl_vsync;

    return glx.SwapIntervalSGI(interval);
}

EXPORT_C_(int) glXSwapIntervalMESA(unsigned int interval) {
#ifndef NDEBUG
    std::cerr << __func__ << ": " << interval << std::endl;
#endif
    glx.Load();

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = (unsigned int)params.gl_vsync;

    return glx.SwapIntervalMESA(interval);
}

EXPORT_C_(int) glXGetSwapIntervalMESA() {
    glx.Load();
    int interval = glx.GetSwapIntervalMESA();

    if (!is_blacklisted()) {
        static bool first_call = true;

        if (first_call) {
            first_call = false;
            if (params.gl_vsync >= 0) {
                interval = params.gl_vsync;
                glx.SwapIntervalMESA(interval);
            }
        }
    }

#ifndef NDEBUG
    std::cerr << __func__ << ": " << interval << std::endl;
#endif
    return interval;
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 10> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(glXGetProcAddress),
   ADD_HOOK(glXGetProcAddressARB),
   ADD_HOOK(glXCreateContext),
   ADD_HOOK(glXMakeCurrent),
   ADD_HOOK(glXSwapBuffers),
   ADD_HOOK(glXSwapBuffersMscOML),

   ADD_HOOK(glXSwapIntervalEXT),
   ADD_HOOK(glXSwapIntervalSGI),
   ADD_HOOK(glXSwapIntervalMESA),
   ADD_HOOK(glXGetSwapIntervalMESA),
#undef ADD_HOOK
}};

EXPORT_C_(void *) mangohud_find_glx_ptr(const char *name)
{
  if (is_blacklisted())
      return nullptr;

   for (auto& func : name_to_funcptr_map) {
      if (strcmp(name, func.name) == 0)
         return func.ptr;
   }

   return nullptr;
}

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName) {
    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = mangohud_find_glx_ptr( (const char*)procName );
    if (func)
        return func;

    return get_glx_proc_address((const char*)procName);
}

EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName) {
    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = mangohud_find_glx_ptr( (const char*)procName );
    if (func)
        return func;

    return get_glx_proc_address((const char*)procName);
}
