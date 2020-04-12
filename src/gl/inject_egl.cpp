#include <iostream>
#include <array>
#include <cstring>
#include "real_dlsym.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"

#include <chrono>
#include <iomanip>

#include "imgui_hud.h"

using namespace MangoHud::GL;

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type
EXPORT_C_(void *) eglGetProcAddress(const char* procName);

void* get_egl_proc_address(const char* name) {

    void *func = nullptr;
    static void *(*pfn_eglGetProcAddress)(const char*) = nullptr;
    if (!pfn_eglGetProcAddress)
        pfn_eglGetProcAddress = reinterpret_cast<decltype(pfn_eglGetProcAddress)>(get_proc_address("eglGetProcAddress"));

    if (pfn_eglGetProcAddress)
        func = pfn_eglGetProcAddress(name);

    if (!func)
        func = get_proc_address( name );

    if (!func) {
        std::cerr << "MANGOHUD: Failed to get function '" << name << "'" << std::endl;
    }

    return func;
}

//EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EXPORT_C_(int) eglMakeCurrent_OFF(void *dpy, void *draw, void *read,void *ctx) {

#ifndef NDEBUG
    std::cerr << __func__ << ": " << draw << ", " << ctx << std::endl;
#endif
    int ret = 0;
    return ret;
}

EXPORT_C_(unsigned int) eglSwapBuffers( void* dpy, void* surf)
{
    static int (*pfn_eglSwapBuffers)(void*, void*) = nullptr;
    if (!pfn_eglSwapBuffers)
        pfn_eglSwapBuffers = reinterpret_cast<decltype(pfn_eglSwapBuffers)>(get_proc_address("eglSwapBuffers"));

    static int (*pfn_eglQuerySurface)(void* dpy, void* surface, int attribute, int *value) = nullptr;
    if (!pfn_eglQuerySurface)
        pfn_eglQuerySurface = reinterpret_cast<decltype(pfn_eglQuerySurface)>(get_proc_address("eglQuerySurface"));


    //std::cerr << __func__ << "\n";

    imgui_create(surf);

    int width=0, height=0;
    if (pfn_eglQuerySurface(dpy, surf, 0x3056, &height) &&
        pfn_eglQuerySurface(dpy, surf, 0x3057, &width))
        imgui_render(width, height);

    //std::cerr << "\t" << width << " x " << height << "\n";

    return pfn_eglSwapBuffers(dpy, surf);
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 1> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(eglGetProcAddress),
#undef ADD_HOOK
}};

void *find_egl_ptr(const char *name)
{
   for (auto& func : name_to_funcptr_map) {
      if (strcmp(name, func.name) == 0)
         return func.ptr;
   }

   return nullptr;
}

EXPORT_C_(void *) eglGetProcAddress(const char* procName) {
    std::cerr << __func__ << ": " << procName << std::endl;

    void* func = find_egl_ptr(procName);
    if (func)
        return func;

    return get_egl_proc_address(procName);
}
