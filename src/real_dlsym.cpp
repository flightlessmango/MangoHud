/**
 * \author Pyry Haulos <pyry.haulos@gmail.com>
 * \date 2007-2008
 * For conditions of distribution and use, see copyright notice in elfhacks.h
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "real_dlsym.h"
#include "elfhacks.h"

void *(*__dlopen)(const char *, int) = nullptr;
void *(*__dlsym)(void *, const char *) = nullptr;
static bool print_dlopen = getenv("MANGOHUD_DEBUG_DLOPEN") != nullptr;
static bool print_dlsym = getenv("MANGOHUD_DEBUG_DLSYM") != nullptr;

void get_real_functions()
{
    eh_obj_t libdl;
    int ret;

#if defined(__GLIBC__)
    ret = eh_find_obj(&libdl, "*libdl.so*");
    if (ret)
#endif
        ret = eh_find_obj(&libdl, "*libc.so*"); // musl, glibc 2.34+

    if (ret) {
        fprintf(stderr, "MANGOHUD: Cannot find libdl.so and libc.so\n");
        exit(1);
    }

    if (eh_find_sym(&libdl, "dlopen", (void **) &__dlopen)) {
        fprintf(stderr, "MANGOHUD: Can't get dlopen()\n");
        exit(1);
    }

    if (eh_find_sym(&libdl, "dlsym", (void **) &__dlsym)) {
        fprintf(stderr, "MANGOHUD: Can't get dlsym()\n");
        exit(1);
    }

    eh_destroy_obj(&libdl);
}

/**
 * \brief dlopen() wrapper, just passes calls to real dlopen()
 *        and writes information to standard output
 */
void *real_dlopen(const char *filename, int flag)
{
    if (__dlopen == nullptr)
        get_real_functions();

    void *result = __dlopen(filename, flag);

    if (print_dlopen) {
        printf("dlopen(%s, ", filename);
        const char *fmt = "%s";
        #define FLAG(test) if (flag & test) { printf(fmt, #test); fmt = "|%s"; }
        FLAG(RTLD_LAZY)
        FLAG(RTLD_NOW)
        FLAG(RTLD_GLOBAL)
        FLAG(RTLD_LOCAL)
        FLAG(RTLD_NODELETE)
        FLAG(RTLD_NOLOAD)
#ifdef RTLD_DEEPBIND
        FLAG(RTLD_DEEPBIND)
#endif
        #undef FLAG
        printf(") = %p\n", result);
    }

    return result;
}

/**
 * \brief dlsym() wrapper, passes calls to real dlsym() and
 *        writes information to standard output
 */
void *real_dlsym(void *handle, const char *symbol)
{
    if (__dlsym == nullptr)
        get_real_functions();

    void *result = __dlsym(handle, symbol);

    if (print_dlsym)
        printf("dlsym(%p, %s) = %p\n", handle, symbol, result);

    return result;
}

void* get_proc_address(const char* name) {
    void (*func)() = (void (*)())real_dlsym(RTLD_NEXT, name);
    return (void*)func;
}

#ifdef HOOK_DLSYM
EXPORT_C_(void *) mangohud_find_glx_ptr(const char *name);
EXPORT_C_(void *) mangohud_find_egl_ptr(const char *name);

EXPORT_C_(void*) dlsym(void * handle, const char * name)
{
    void* func = nullptr;
#ifdef HAVE_X11
    func = mangohud_find_glx_ptr(name);
    if (func) {
        //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
        return func;
    }
#endif

    func = mangohud_find_egl_ptr(name);
    if (func) {
        //fprintf(stderr,"%s: local: %s\n",  __func__ , name);
        return func;
    }

    //fprintf(stderr,"%s: foreign: %s\n",  __func__ , name);
    return real_dlsym(handle, name);
}
#endif
