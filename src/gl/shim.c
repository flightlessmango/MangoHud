#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include "gl.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

static void* handle = NULL;
static bool mangoHudLoaded = false;

static void loadMangoHud(void);
static void loadMangoHud() {
    if (mangoHudLoaded) return;

    //allow user to load custom mangohud libs (useful for testing)
    char *libdir = getenv("MANGOHUD_OPENGL_LIB");

    if (libdir)
    {
        handle = dlopen(libdir, RTLD_LAZY | RTLD_LOCAL | RTLD_DEEPBIND);

        if (handle) mangoHudLoaded = true;
        else {
            fprintf(stderr, "shim: Failed to load from: %s\n", libdir);
            libdir = NULL;
        }
    }

    // Load MangoHud after GLX functions have been intercepted
    if(!mangoHudLoaded)
    {
        handle = dlopen("${ORIGIN}/libMangoHud_opengl.so", RTLD_LAZY | RTLD_LOCAL | RTLD_DEEPBIND);
        if (handle) mangoHudLoaded = true;
    }

    if (mangoHudLoaded) {
        printf("shim: Loaded mangohud library\n");
        if (libdir) printf("from custom location: %s\n", libdir);
    }
}

void glXSwapBuffers(void* dpy, void* drawable) {
    loadMangoHud();

    void (*pglXSwapBuffers)(void*, void*) = dlsym(handle, "glXSwapBuffers");
    if (pglXSwapBuffers) {
        pglXSwapBuffers(dpy, drawable);
    } else {
       pglXSwapBuffers = dlsym(RTLD_NEXT, "glXSwapBuffers");
        if (pglXSwapBuffers)
            pglXSwapBuffers(dpy, drawable);
    }
}

void* eglGetDisplay(void *native_dpy) {
    loadMangoHud();

    void* (*peglGetDisplay)(void*) = dlsym(handle, "eglGetDisplay");
    if (peglGetDisplay) {
        return peglGetDisplay(native_dpy);
    } else {
        peglGetDisplay = dlsym(RTLD_NEXT, "eglGetDisplay");
        if (peglGetDisplay)
            return peglGetDisplay(native_dpy);
    }

    return NULL;
}

void* eglGetPlatformDisplay(unsigned int platform, void* native_display, const intptr_t* attrib_list)
{
    loadMangoHud();

    void* (*peglGetPlatformDisplay)(unsigned int, void*, const intptr_t*) = dlsym(handle, "eglGetPlatformDisplay");
    if (peglGetPlatformDisplay) {
        return peglGetPlatformDisplay(platform, native_display, attrib_list);
    } else {
        peglGetPlatformDisplay = dlsym(RTLD_NEXT, "eglGetPlatformDisplay");
        if (peglGetPlatformDisplay)
            return peglGetPlatformDisplay(platform, native_display, attrib_list);
    }

    return NULL;
}

unsigned int eglSwapBuffers(void* dpy, void* surf) {
    loadMangoHud();

    // Get the hooked eglSwapBuffers function from the loaded library if available
    unsigned int (*peglSwapBuffers)(void*, void*) = dlsym(handle, "eglSwapBuffers");
    if (peglSwapBuffers) {
        return peglSwapBuffers(dpy, surf);;
    } else {
        // Fall back to the original eglSwapBuffers function
        peglSwapBuffers = dlsym(RTLD_NEXT, "eglSwapBuffers");
        if (peglSwapBuffers) {
            return peglSwapBuffers(dpy, surf);
        }
    }

    return 0;
}

int64_t glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    loadMangoHud();

    // Get the hooked glXSwapBuffersMscOML function from the loaded library if available
    int64_t (*pglXSwapBuffersMscOML)(void*, void*, int64_t, int64_t, int64_t) = dlsym(handle, "glXSwapBuffersMscOML");
    if (pglXSwapBuffersMscOML) {
        return pglXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
    } else {
        // Fall back to the original glXSwapBuffersMscOML function
        pglXSwapBuffersMscOML = dlsym(RTLD_NEXT, "glXSwapBuffersMscOML");
        if (pglXSwapBuffersMscOML) {
            return pglXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
        }
    }

    return -1;
}
