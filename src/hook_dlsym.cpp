#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <link.h>
#include "real_dlsym.h"
#include <string.h>
#include <unistd.h>

#include <string>

void *egl_handle = NULL;

EXPORT_C_(void*) dlsym(void * handle, const char * name)
{
    static void *(*find_glx_ptr)(const char *name) = nullptr;
    static void *(*find_egl_ptr)(const char *name) = nullptr;

    if (!find_glx_ptr)
        find_glx_ptr = reinterpret_cast<decltype(find_glx_ptr)> (real_dlsym(RTLD_NEXT, "mangohud_find_glx_ptr"));

    if (!find_egl_ptr)
        find_egl_ptr = reinterpret_cast<decltype(find_egl_ptr)> (real_dlsym(RTLD_NEXT, "mangohud_find_egl_ptr"));

    char path_real[4096] = {0};
    //pick a libEGL to use since ANGLE uses the same file name
    if(handle != RTLD_DEFAULT && handle != RTLD_NEXT)
    {
        struct link_map *path;
        dlinfo(handle, RTLD_DI_LINKMAP, &path);

        int ret = readlink(path->l_name, path_real, 4096);
        if (ret < 0) {
            strcpy(path_real, path->l_name);
        }

        if (strstr(path_real, "libEGL") && !egl_handle) {
            egl_handle = handle;
        }
    }

    void* func = nullptr;
    void* real_func = real_dlsym(handle, name);

    if (find_glx_ptr && real_func) {
        func = find_glx_ptr(name);
        if (func) {
            //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
            return func;
        }
    }

    if (find_egl_ptr && real_func && egl_handle == handle) {
        fprintf(stderr, "name %s\n", name);
        func = find_egl_ptr(name);
        if (func) {
            //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
            return func;
        }
    }

    //fprintf(stderr,"%s: foreign: %s\n",  __func__ , name);
    return real_func;
}
