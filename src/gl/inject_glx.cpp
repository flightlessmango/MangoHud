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
#include "gl_hud.h"
#include "../config.h"

using namespace MangoHud::GL;

#ifndef GLX_WIDTH
#define GLX_WIDTH   0x801D
#define GLX_HEIGHT  0x801E
#endif

static glx_loader glx;

static std::atomic<int> refcnt (0);

static bool swap_interval_set = false;
static void* last_ctx;

static void* get_glx_proc_address(const char* name) {
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

bool glx_mesa_queryInteger(int attrib, unsigned int *value);
bool glx_mesa_queryInteger(int attrib, unsigned int *value)
{
    static int (*pfn_queryInteger)(int attribute, unsigned int *value) =
        reinterpret_cast<decltype(pfn_queryInteger)>(get_glx_proc_address(
                    "glXQueryCurrentRendererIntegerMESA"));
    if (pfn_queryInteger)
        return !!pfn_queryInteger(attrib, value);
    return false;
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

EXPORT_C_(void *) glXCreateContextAttribs(void *dpy, void *config,void *share_context, int direct, const int *attrib_list);
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
            imgui_set_context(ctx, gl_wsi::GL_WSI_GLX);
            SPDLOG_DEBUG("GL ref count: {}", refcnt.load());
        }
    }

    return ret;
}

#ifndef GLX_SWAP_INTERVAL_EXT
#define GLX_SWAP_INTERVAL_EXT 0x20F1
#endif

static void do_imgui_swap(void *dpy, void *drawable)
{
    static auto last_time = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();

    // if bufferSize is 0 then glXQueryDrawable is probably not working
    // this is the case with llvmpipe
    unsigned int bufferSize;
    glx.QueryDrawable(dpy, drawable, GL_BUFFER_SIZE, &bufferSize);

    std::chrono::duration<double> elapsed_seconds = current_time - last_time;
    if (bufferSize != 0 && (HUDElements.vsync == 10 || elapsed_seconds.count() > 5.0))
        glx.QueryDrawable(dpy, drawable, GLX_SWAP_INTERVAL_EXT, &HUDElements.vsync);

    GLint vp[4];
    if (!is_blacklisted()) {
        imgui_create(glx.GetCurrentContext(), gl_wsi::GL_WSI_GLX);

        unsigned int width = -1, height = -1;

        switch (get_params()->gl_size_query)
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

static void set_swap_interval(void* dpy, void* drawable)
{
    glx.Load();

    if (!is_blacklisted()) {
        void* ctx = glx.GetCurrentContext();

        if (ctx == last_ctx && swap_interval_set)
            return;

        imgui_create(ctx, gl_wsi::GL_WSI_GLX);

        auto real_params = get_params();
        SPDLOG_DEBUG("got params, gl_vsync {}", real_params->gl_vsync);
        // Afaik -1 only works with EXT version if it has GLX_EXT_swap_control_tear, maybe EGL_MESA_swap_control_tear someday
        if (real_params->gl_vsync >= -1) {
            if (glx.SwapIntervalEXT && dpy && drawable)
                glx.SwapIntervalEXT(dpy, drawable, real_params->gl_vsync);
        }
        if (real_params->gl_vsync >= 0) {
            if (glx.SwapIntervalSGI)
                glx.SwapIntervalSGI(real_params->gl_vsync);
            if (glx.SwapIntervalMESA)
                glx.SwapIntervalMESA(real_params->gl_vsync);
        }
        swap_interval_set = true;
    }
}

EXPORT_C_(void) glXSwapBuffers(void* dpy, void* drawable) {
    glx.Load();

    do_imgui_swap(dpy, drawable);

    set_swap_interval(dpy, drawable);

    using namespace std::chrono_literals;
    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_EARLY){
        fps_limit_stats.frameStart = Clock::now();
        FpsLimiter(fps_limit_stats);
        fps_limit_stats.frameEnd = Clock::now();
    }
    glx.SwapBuffers(dpy, drawable);
    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_LATE){
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
    set_swap_interval(dpy, drawable);
    using namespace std::chrono_literals;
    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_EARLY){
        fps_limit_stats.frameStart = Clock::now();
        FpsLimiter(fps_limit_stats);
        fps_limit_stats.frameEnd = Clock::now();
    }

    int64_t ret = glx.SwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);

    if (!is_blacklisted() && fps_limit_stats.targetFrameTime > 0s && fps_limit_stats.method == FPS_LIMIT_METHOD_LATE){
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

    swap_interval_set = false;
    set_swap_interval(dpy, draw);
}

EXPORT_C_(int) glXSwapIntervalSGI(int interval) {
    SPDLOG_DEBUG("{}: {}", __func__, interval);
    glx.Load();
    if (!glx.SwapIntervalSGI)
        return -1;

    swap_interval_set = false;
    set_swap_interval(nullptr, nullptr);
    return 0;
}

EXPORT_C_(int) glXSwapIntervalMESA(unsigned int interval) {
    SPDLOG_DEBUG("{}: {}", __func__, interval);
    glx.Load();
    if (!glx.SwapIntervalMESA)
        return -1;

    swap_interval_set = false;
    set_swap_interval(nullptr, nullptr);
    return 0;
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
            void* ctx = glx.GetCurrentContext();
            imgui_create(ctx, gl_wsi::GL_WSI_GLX);
            if (get_params()->gl_vsync >= 0) {
                interval = get_params()->gl_vsync;
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

EXPORT_C_(void *) mangohud_find_glx_ptr(const char *name);
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
    SPDLOG_TRACE("{}: '{}', real: {}, fun: {}", __func__, reinterpret_cast<const char*>(procName), real_func, func);

    if (func && real_func)
        return func;

    return real_func;
}

EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName) {
    void *real_func = get_glx_proc_address((const char*)procName);
    void *func = mangohud_find_glx_ptr( (const char*)procName );
    SPDLOG_TRACE("{}: '{}', real: {}, fun: {}", __func__, reinterpret_cast<const char*>(procName), real_func, func);
    if (func && real_func)
        return func;

    return real_func;
}
