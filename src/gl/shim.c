#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <stdio.h>
#include <stdint.h>
#include "gl.h"
#include "real_dlsym.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#ifndef RTLD_DEEPBIND
#define RTLD_DEEPBIND 0
#endif

static void* handle = NULL;
static bool mangoHudLoaded = false;

#ifdef __GLIBC__
static inline void
free_indirect(char **p)
{
    free(*p);
}

static bool load_adjacent_opengl_lib(void)
{
    __attribute__((cleanup(free_indirect))) char *location = NULL;
    __attribute__((cleanup(free_indirect))) char *lib = NULL;
    Dl_info info = {};
    void *extra_info = NULL;

    // The first argument can be any symbol in this shared library,
    // mangoHudLoaded is a convenient one
    if (!dladdr1(&mangoHudLoaded, &info, &extra_info, RTLD_DL_LINKMAP))
    {
        fprintf(stderr, "shim: Unable to find my own location: %s\n", dlerror());
        return false;
    }

    const struct link_map *map = extra_info;
    if (map == NULL)
    {
        fprintf(stderr, "shim: Unable to find my own location: NULL link_map\n");
        return false;
    }
    if (map->l_name == NULL)
    {
        fprintf(stderr, "shim: Unable to find my own location: NULL l_name\n");
        return false;
    }

    location = realpath(map->l_name, NULL);
    char *slash = strrchr(location, '/');

    if (slash == NULL)
    {
        fprintf(stderr, "shim: Unable to find my own location: no directory separator\n");
        return false;
    }

    *slash = '\0';

    if (asprintf(&lib, "%s/libMangoHud_opengl.so", location) < 0)
    {
        fprintf(stderr, "shim: asprintf: %s\n", strerror(errno));
        return false;
    }

    handle = dlopen(lib, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);

    if (handle == NULL)
    {
        fprintf(stderr, "shim: Failed to load from \"%s\": %s\n", lib, dlerror());
        return false;
    }

    return true;
}
#endif

// Load MangoHud after EGL/GLX functions have been intercepted
static void loadMangoHud(void);
static void loadMangoHud() {
    if (mangoHudLoaded) return;

    // allow user to load custom mangohud libs (useful for testing)
    char *libs = getenv("MANGOHUD_OPENGL_LIBS");
    char *lib = NULL;

    if (libs)
    {
        lib = strtok(libs, ":");

        // when user specifies only one path
        if (!lib) lib = libs;

        while (lib != NULL)
        {
            handle = dlopen(lib, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);

            if (handle)
            {
                mangoHudLoaded = true;
                return;
            }
            else fprintf(stderr, "shim: Failed to load from \"%s\": %s\n", lib, dlerror());

            lib = strtok(NULL, ":");
        }
    }

#ifdef __GLIBC__
    if (load_adjacent_opengl_lib())
    {
        mangoHudLoaded = true;
        return;
    }
#endif

    if (!mangoHudLoaded)
    {
        handle = dlopen("${ORIGIN}/libMangoHud_opengl.so", RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
        if (handle) mangoHudLoaded = true;
        else
        {
            fprintf(stderr, "shim: Failed to load from ${ORIGIN}/libMangoHud_opengl.so: %s\n", dlerror());
            handle = RTLD_NEXT;
        }
    }
}

#define CREATE_FWD_VOID(name, params, ...) \
    void name params { \
        loadMangoHud(); \
        void (*p##name) params = real_dlsym(handle, #name); \
        if (p##name) p##name(__VA_ARGS__); \
    }
#define CREATE_FWD(ret_type, name, params, ...) \
    ret_type name params { \
        loadMangoHud(); \
        ret_type (*p##name) params = real_dlsym(handle, #name); \
        if (p##name) return p##name(__VA_ARGS__); \
        return (ret_type)0; \
    }

#ifdef HAVE_X11
CREATE_FWD_VOID(glXSwapBuffers, (void* dpy, void* drawable), dpy, drawable)
CREATE_FWD(int64_t, glXSwapBuffersMscOML,
           (void* dpy, void* drawable, int64_t target_msc,
            int64_t divisor, int64_t remainder),
           dpy, drawable, target_msc, divisor, remainder)
CREATE_FWD(void*, glXGetProcAddress, (const unsigned char* procName), procName)
CREATE_FWD(void*, glXGetProcAddressARB, (const unsigned char* procName), procName)
#endif
CREATE_FWD(void*, eglGetDisplay, (void* native_dpy), native_dpy)
CREATE_FWD(void*, eglGetPlatformDisplay,
    (unsigned int platform,
     void* native_display,
     const intptr_t* attrib_list),
     platform, native_display, attrib_list)
CREATE_FWD(unsigned int, eglSwapBuffers, (void* dpy, void* surf), dpy, surf)
CREATE_FWD(void*, eglGetProcAddress, (const char* procName), procName)
CREATE_FWD(int, eglTerminate, (void *display), display)

#undef CREATE_FWD
#undef CREATE_FWD_VOID

struct func_ptr {
    const char* name;
    void* ptr;
};

#define ADD_HOOK(fn) { #fn, (void*)fn }
static struct func_ptr hooks[] = {
#ifdef HAVE_X11
    ADD_HOOK(glXGetProcAddress),
    ADD_HOOK(glXGetProcAddressARB),
    ADD_HOOK(glXSwapBuffers),
    ADD_HOOK(glXSwapBuffersMscOML),
#endif
    ADD_HOOK(eglSwapBuffers),
    ADD_HOOK(eglGetPlatformDisplay),
    ADD_HOOK(eglGetDisplay),
    ADD_HOOK(eglGetProcAddress),
    ADD_HOOK(eglTerminate)
};
#undef ADD_HOOK

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) sizeof(arr)/sizeof(arr[0])
#endif

#include <stddef.h>

// Glibc has nonconformance behaviour
// which is Glibc clears dlerror if dlsym succeded instead
// leaving it as it is as required by standard
// Reference: https://pubs.opengroup.org/onlinepubs/009604299/functions/dlsym.html
//
// Lets disable dlerror override to avoid risking programs
// which already relying on Glibc's behaviour so to keep
// glibc's behaviour
// while on conformance system such as Musl it fixes bugs
#ifdef __GLIBC__
# define USE_DLERROR_OVERRIDE 0
#else
# define USE_DLERROR_OVERRIDE 1
#endif

static _Thread_local char *error_override = NULL;
static _Thread_local bool activate_override = false;

char *dlerror(void)
{
    if (!USE_DLERROR_OVERRIDE)
       return real_dlerror();
    
    if (activate_override)
    {
        // Deactivate the override for next call to allow
        // real_dlerror to be used
        activate_override = false;
        return error_override;
    } else if (error_override) {
        // Free resources that was allocated for error override
        // because the override is now deactivated
        free(error_override);
        error_override = NULL;
    }

    return real_dlerror();
}

static void save_and_consume_real_dlerror()
{
    if (!USE_DLERROR_OVERRIDE)
       return;
    
    char* recent_error = real_dlerror();
    if (!recent_error)
    {
        error_override = recent_error;
        return;
    }
    
    size_t error_length = strlen(recent_error);
    error_override = malloc(error_length + 1);
    memcpy(error_override, recent_error, error_length + 1);
}

void* dlsym(void *handle, const char *name)
{
    save_and_consume_real_dlerror();
    const char* dlsym_enabled = getenv("MANGOHUD_DLSYM");
    void* is_angle = real_dlsym(handle, "eglStreamPostD3DTextureANGLE");
    // Consume error message if there was an error
    if (!is_angle)
    {
        (void) real_dlerror();
    }
    void* fn_ptr = real_dlsym(handle, name);
    // Activate override if there wasn't any new error
    // from real_dlsym
    activate_override = fn_ptr != NULL;

    if (!is_angle && fn_ptr && (!dlsym_enabled || dlsym_enabled[0] != '0'))
    {
        for (unsigned i = 0; i < ARRAY_SIZE(hooks); i++)
        {
            if (!strcmp(hooks[i].name, name))
            {
                return hooks[i].ptr;
            }
        }
    }

    return fn_ptr;
}
