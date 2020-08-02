#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "real_dlsym.h"

EXPORT_C_(void*) dlsym(void * handle, const char * name)
{
    static void *(*find_glx_ptr)(const char *name) = nullptr;
    static void *(*find_egl_ptr)(const char *name) = nullptr;

    if (!find_glx_ptr)
        find_glx_ptr = reinterpret_cast<decltype(find_glx_ptr)> (real_dlsym(RTLD_NEXT, "mangohud_find_glx_ptr"));

    if (!find_egl_ptr)
        find_egl_ptr = reinterpret_cast<decltype(find_egl_ptr)> (real_dlsym(RTLD_NEXT, "mangohud_find_egl_ptr"));

    void* func = nullptr;

    if (find_glx_ptr) {
        func = find_glx_ptr(name);
        if (func) {
            //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
            return func;
        }
    }

    if (find_egl_ptr) {
        func = find_egl_ptr(name);
        if (func) {
            //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
            return func;
        }
    }

    //fprintf(stderr,"%s: foreign: %s\n",  __func__ , name);
    return real_dlsym(handle, name);
}
