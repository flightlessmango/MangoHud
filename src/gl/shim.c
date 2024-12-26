#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include "gl.h"
#include "real_dlsym.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

static void* handle = NULL;
static bool mangoHudLoaded = false;

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
                break;
            }
            else fprintf(stderr, "shim: Failed to load from: %s\n", lib);

            lib = strtok(NULL, ":");
        }
    }

    if (!mangoHudLoaded)
    {
        handle = dlopen("${ORIGIN}/libMangoHud_opengl.so", RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
        if (handle) mangoHudLoaded = true;
    }
}

void glXSwapBuffers(void* dpy, void* drawable) {
    loadMangoHud();

    void (*pglXSwapBuffers)(void*, void*) = real_dlsym(handle, "glXSwapBuffers");
    if (pglXSwapBuffers) {
        pglXSwapBuffers(dpy, drawable);
    } else {
       pglXSwapBuffers = real_dlsym(RTLD_NEXT, "glXSwapBuffers");
        if (pglXSwapBuffers)
            pglXSwapBuffers(dpy, drawable);
    }
}

void* eglGetDisplay(void *native_dpy) {
    loadMangoHud();

    void* (*peglGetDisplay)(void*) = real_dlsym(handle, "eglGetDisplay");
    if (peglGetDisplay) {
        return peglGetDisplay(native_dpy);
    } else {
        peglGetDisplay = real_dlsym(RTLD_NEXT, "eglGetDisplay");
        if (peglGetDisplay)
            return peglGetDisplay(native_dpy);
    }

    return NULL;
}

void* eglGetPlatformDisplay(unsigned int platform, void* native_display, const intptr_t* attrib_list)
{
    loadMangoHud();

    void* (*peglGetPlatformDisplay)(unsigned int, void*, const intptr_t*) = real_dlsym(handle, "eglGetPlatformDisplay");
    if (peglGetPlatformDisplay) {
        return peglGetPlatformDisplay(platform, native_display, attrib_list);
    } else {
        peglGetPlatformDisplay = real_dlsym(RTLD_NEXT, "eglGetPlatformDisplay");
        if (peglGetPlatformDisplay)
            return peglGetPlatformDisplay(platform, native_display, attrib_list);
    }

    return NULL;
}

unsigned int eglSwapBuffers(void* dpy, void* surf) {
    loadMangoHud();

    // Get the hooked eglSwapBuffers function from the loaded library if available
    unsigned int (*peglSwapBuffers)(void*, void*) = real_dlsym(handle, "eglSwapBuffers");
    if (peglSwapBuffers) {
        return peglSwapBuffers(dpy, surf);
    } else {
        // Fall back to the original eglSwapBuffers function
        peglSwapBuffers = real_dlsym(RTLD_NEXT, "eglSwapBuffers");
        if (peglSwapBuffers) {
            return peglSwapBuffers(dpy, surf);
        }
    }

    return 0;
}

int64_t glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    loadMangoHud();

    // Get the hooked glXSwapBuffersMscOML function from the loaded library if available
    int64_t (*pglXSwapBuffersMscOML)(void*, void*, int64_t, int64_t, int64_t) = real_dlsym(handle, "glXSwapBuffersMscOML");
    if (pglXSwapBuffersMscOML) {
        return pglXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
    } else {
        // Fall back to the original glXSwapBuffersMscOML function
        pglXSwapBuffersMscOML = real_dlsym(RTLD_NEXT, "glXSwapBuffersMscOML");
        if (pglXSwapBuffersMscOML) {
            return pglXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
        }
    }

    return -1;
}

void* glXGetProcAddress(const unsigned char* procName)
{
    loadMangoHud();

    void* (*pglXGetProcAddress)(const unsigned char*) = real_dlsym(handle, "glXGetProcAddress");
    if (pglXGetProcAddress) {
        return pglXGetProcAddress(procName);
    } else {
        pglXGetProcAddress = real_dlsym(RTLD_NEXT, "glXGetProcAddress");
        if (pglXGetProcAddress) {
            return pglXGetProcAddress(procName);
        }
    }

    return NULL;
}

void* glXGetProcAddressARB(const unsigned char* procName)
{
    loadMangoHud();

    void* (*pglXGetProcAddressARB)(const unsigned char*) = real_dlsym(handle, "glXGetProcAddressARB");
    if (pglXGetProcAddressARB) {
        return pglXGetProcAddressARB(procName);
    } else {
        pglXGetProcAddressARB = real_dlsym(RTLD_NEXT, "glXGetProcAddressARB");
        if (pglXGetProcAddressARB) {
            return pglXGetProcAddressARB(procName);
        }
    }

    return NULL;
}

void* eglGetProcAddress(const char *procName)
{
    loadMangoHud();

    void* (*peglGetProcAddress)(const char*) = real_dlsym(handle, "eglGetProcAddress");
    if (peglGetProcAddress) {
        return peglGetProcAddress(procName);
    } else {
        peglGetProcAddress = real_dlsym(RTLD_NEXT, "eglGetProcAddress");
        if (peglGetProcAddress) {
            return peglGetProcAddress(procName);
        }
    }

    return NULL;
}

struct func_ptr {
    const char* name;
    void* ptr;
};

#define ADD_HOOK(fn) { #fn, (void*)fn }
static struct func_ptr hooks[] = {
    ADD_HOOK(glXGetProcAddress),
    ADD_HOOK(glXGetProcAddressARB),
    ADD_HOOK(glXSwapBuffers),
    ADD_HOOK(glXSwapBuffersMscOML),
    ADD_HOOK(eglSwapBuffers),
    ADD_HOOK(eglGetPlatformDisplay),
    ADD_HOOK(eglGetDisplay),
    ADD_HOOK(eglGetProcAddress)
};
#undef ADD_HOOK

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) sizeof(arr)/sizeof(arr[0])
#endif

void* dlsym(void *handle, const char *name)
{
    const char* dlsym_enabled = getenv("MANGOHUD_DLSYM");
    void* is_angle = real_dlsym(handle, "eglStreamPostD3DTextureANGLE");
    void* fn_ptr = real_dlsym(handle, name);

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
