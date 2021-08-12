#include <X11/Xlib.h>
#include <iostream>
#include <array>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <spdlog/spdlog.h>
#include "real_dlsym.h"
#include "loaders/loader_glx.h"
#include "loaders/loader_x11.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"
#include "blacklist.h"

#include <chrono>
#include <iomanip>

#include <glad/glad.h>
#include "imgui_hud.h"

using namespace MangoHud::GL;

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName);
EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName);

#ifndef GLX_WIDTH
#define GLX_WIDTH   0x801D
#define GLX_HEIGHT  0x801E
#endif

static glx_loader glx;

static std::atomic<int> refcnt (0);

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
        SPDLOG_ERROR("Failed to get function '{}'", name);
    }

    return func;
}

EXPORT_C_(void *) glXCreateContext(void *dpy, void *vis, void *shareList, int direct)
{
    glx.Load();
    void *ctx = glx.CreateContext(dpy, vis, shareList, direct);
    if (ctx)
        refcnt++;
    SPDLOG_DEBUG("{}: {}", __func__,  ctx);
    return ctx;
}

EXPORT_C_(void *) glXCreateContextAttribs(void *dpy, void *config,void *share_context, int direct, const int *attrib_list)
{
    glx.Load();
    void *ctx = glx.CreateContextAttribs(dpy, config, share_context, direct, attrib_list);
    if (ctx)
        refcnt++;
    SPDLOG_DEBUG("{}: {}", __func__,  ctx);
    return ctx;
}

EXPORT_C_(void *) glXCreateContextAttribsARB(void *dpy, void *config,void *share_context, int direct, const int *attrib_list)
{
    glx.Load();
    void *ctx = glx.CreateContextAttribsARB(dpy, config, share_context, direct, attrib_list);
    if (ctx)
        refcnt++;
    SPDLOG_DEBUG("{}: {}", __func__,  ctx);
    return ctx;
}

EXPORT_C_(void) glXDestroyContext(void *dpy, void *ctx)
{
    glx.Load();
    glx.DestroyContext(dpy, ctx);
    refcnt--;
    if (refcnt <= 0)
        imgui_shutdown();
    SPDLOG_DEBUG("{}: {}", __func__,  ctx);
}

EXPORT_C_(int) glXMakeCurrent(void* dpy, void* drawable, void* ctx) {
    glx.Load();
    SPDLOG_DEBUG("{}: {}, {}", __func__, drawable, ctx);
    int ret = glx.MakeCurrent(dpy, drawable, ctx);

    if (!is_blacklisted()) {
        if (ret) {
            if (ctx)
                imgui_create(ctx, gl_platform::GLX);
            SPDLOG_DEBUG("GL ref count: {}", refcnt);
        }

        // Afaik -1 only works with EXT version if it has GLX_EXT_swap_control_tear, maybe EGL_MESA_swap_control_tear someday
        if (params.gl_vsync >= -1) {
            if (glx.SwapIntervalEXT)
                glx.SwapIntervalEXT(dpy, drawable, params.gl_vsync);
        }
        if (params.gl_vsync >= 0) {
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
    GLint vp[4];
    if (!is_blacklisted()) {
        imgui_create(glx.GetCurrentContext(), gl_platform::GLX);

        unsigned int width = -1, height = -1;

        switch (params.gl_size_query)
        {
            case GL_SIZE_VIEWPORT:
                glGetIntegerv (GL_VIEWPORT, vp);
                width = vp[2];
                height = vp[3];
                break;
            case GL_SIZE_SCISSORBOX:
                glGetIntegerv (GL_SCISSOR_BOX, vp);
                width = vp[2];
                height = vp[3];
                break;
            default:
                glx.QueryDrawable(dpy, drawable, GLX_WIDTH, &width);
                glx.QueryDrawable(dpy, drawable, GLX_HEIGHT, &height);
                break;
        }

        SPDLOG_TRACE("swap buffers: {}x{}", width, height);
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
    if (!glx.SwapBuffersMscOML)
        return -1;

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
    SPDLOG_DEBUG("{}: {}", __func__, interval);
    glx.Load();
    if (!glx.SwapIntervalEXT)
        return;

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = params.gl_vsync;

    glx.SwapIntervalEXT(dpy, draw, interval);
}

EXPORT_C_(int) glXSwapIntervalSGI(int interval) {
    SPDLOG_DEBUG("{}: {}", __func__, interval);
    glx.Load();
    if (!glx.SwapIntervalSGI)
        return -1;

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = params.gl_vsync;

    return glx.SwapIntervalSGI(interval);
}

EXPORT_C_(int) glXSwapIntervalMESA(unsigned int interval) {
    SPDLOG_DEBUG("{}: {}", __func__, interval);
    glx.Load();
    if (!glx.SwapIntervalMESA)
        return -1;

    if (!is_blacklisted() && params.gl_vsync >= 0)
        interval = (unsigned int)params.gl_vsync;

    return glx.SwapIntervalMESA(interval);
}

EXPORT_C_(int) glXGetSwapIntervalMESA() {
    glx.Load();
    if (!glx.GetSwapIntervalMESA)
        return 0;

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

    SPDLOG_DEBUG("{}: {}", __func__, interval);
    return interval;
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 13> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(glXGetProcAddress),
   ADD_HOOK(glXGetProcAddressARB),
   ADD_HOOK(glXCreateContextAttribs),
   ADD_HOOK(glXCreateContextAttribsARB),
   ADD_HOOK(glXCreateContext),
   ADD_HOOK(glXDestroyContext),
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
    void *real_func = get_glx_proc_address((const char*)procName);
    void *func = mangohud_find_glx_ptr( (const char*)procName );
    SPDLOG_TRACE("{}: '{}', real: {}, fun: {}", __func__, procName, real_func, func);

    if (func && real_func)
        return func;

    return real_func;
}

EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName) {
    void *real_func = get_glx_proc_address((const char*)procName);
    void *func = mangohud_find_glx_ptr( (const char*)procName );
    SPDLOG_TRACE("{}: '{}', real: {}, fun: {}", __func__, procName, real_func, func);
    if (func && real_func)
        return func;

    return real_func;
}
