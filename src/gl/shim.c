#include <dlfcn.h>
#include <stdio.h>
#include "gl.h"
#include <string.h>
#include <stdbool.h>
static void* handle = NULL;
static bool mangoHudLoaded = false;

static void loadMangoHud(void);
static void loadMangoHud() {
    if (mangoHudLoaded)
        return;

    // Load MangoHud after GLX functions have been intercepted
    handle = dlopen("${ORIGIN}/libMangoHud_opengl.so", RTLD_LAZY | RTLD_LOCAL| RTLD_DEEPBIND);
    if (handle) 
        mangoHudLoaded = true;
}

void glXSwapBuffers(void* dpy, void* drawable) {
    loadMangoHud();

    void (*lib_glXSwapBuffers)(void*, void*) = dlsym(handle, "glXSwapBuffers");
    if (lib_glXSwapBuffers) {
        lib_glXSwapBuffers(dpy, drawable);
    } else {
        void (*real_glXSwapBuffers)(void*, void*) = dlsym(RTLD_NEXT, "glXSwapBuffers");
        if (real_glXSwapBuffers) 
            real_glXSwapBuffers(dpy, drawable);
    }
}

unsigned int eglSwapBuffers(void* dpy, void* surf) {
    loadMangoHud();

    // Get the hooked eglSwapBuffers function from the loaded library if available
    void (*lib_eglSwapBuffers)(void*, void*) = dlsym(handle, "eglSwapBuffers");
    if (lib_eglSwapBuffers) {
        lib_eglSwapBuffers(dpy, surf);
        return 1;
    } else {
        // Fall back to the original eglSwapBuffers function
        unsigned int (*real_eglSwapBuffers)(void*, void*) = dlsym(RTLD_NEXT, "eglSwapBuffers");
        if (real_eglSwapBuffers) {
            return real_eglSwapBuffers(dpy, surf);
        } else {
            return 0;
        }
    }
}

int64_t glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder) {
    loadMangoHud();

    // Get the hooked glXSwapBuffersMscOML function from the loaded library if available
    int64_t (*lib_glXSwapBuffersMscOML)(void*, void*, int64_t, int64_t, int64_t) = dlsym(handle, "glXSwapBuffersMscOML");
    if (lib_glXSwapBuffersMscOML) {
        return lib_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
    } else {
        // Fall back to the original glXSwapBuffersMscOML function
        int64_t (*real_glXSwapBuffersMscOML)(void*, void*, int64_t, int64_t, int64_t) = dlsym(RTLD_NEXT, "glXSwapBuffersMscOML");
        if (real_glXSwapBuffersMscOML) {
            return real_glXSwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
        } else {
            return -1;
        }
    }
}
