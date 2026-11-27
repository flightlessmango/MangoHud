#include <dlfcn.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "real_dlsym.h"

#ifdef HAVE_WAYLAND_COLOR_TRACKING
#include <wayland-client.h>
static _Atomic(void*) color_library;
static _Thread_local int loading_color_library;

static void* color_hook(const char* name)
{
    void* library = atomic_load(&color_library);
    if (!library && !loading_color_library) {
        loading_color_library = 1;
        Dl_info info = {0};
        if (dladdr(&color_library, &info) && info.dli_fname) {
            char* path = realpath(info.dli_fname, NULL);
            char* slash = path ? strrchr(path, '/') : NULL;
            if (slash) {
                *slash = '\0';
                char* target = NULL;
                if (asprintf(&target, "%s/libMangoHud.so", path) >= 0) {
                    library = real_dlopen(target, RTLD_NOW | RTLD_LOCAL);
                    free(target);
                }
            }
            free(path);
        }
        if (library) {
            void* expected = NULL;
            if (!atomic_compare_exchange_strong(&color_library, &expected, library)) {
                dlclose(library);
                library = expected;
            }
        }
        loading_color_library = 0;
    }
    return library ? real_dlsym(library, name) : NULL;
}

static uint32_t proxy_version(struct wl_proxy* proxy)
{
    uint32_t (*real)(struct wl_proxy*) = real_dlsym(RTLD_NEXT, "wl_proxy_get_version");
    return real(proxy);
}

static struct wl_proxy* marshal_array(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, uint32_t version, uint32_t flags, union wl_argument* args)
{
    typedef struct wl_proxy* (*fn)(struct wl_proxy*, uint32_t, const struct wl_interface*,
                                   uint32_t, uint32_t, union wl_argument*);
    fn hook = color_hook("mangohud_wl_marshal_array");
    fn real = hook ? hook : (fn)real_dlsym(RTLD_NEXT, "wl_proxy_marshal_array_flags");
    return real(proxy, opcode, interface, version, flags, args);
}

static struct wl_proxy* marshal_va(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, uint32_t version, uint32_t flags, va_list args)
{
    typedef struct wl_proxy* (*fn)(struct wl_proxy*, uint32_t, const struct wl_interface*,
                                   uint32_t, uint32_t, va_list);
    fn hook = color_hook("mangohud_wl_marshal_va");
    if (hook)
        return hook(proxy, opcode, interface, version, flags, args);

    const struct wl_interface* (*get_interface)(struct wl_proxy*) =
        real_dlsym(RTLD_NEXT, "wl_proxy_get_interface");
    const struct wl_interface* source = get_interface(proxy);
    if (!source || opcode >= (uint32_t)source->method_count)
        return NULL;
    union wl_argument values[20] = {0};
    size_t count = 0;
    for (const char* sig = source->methods[opcode].signature; *sig; ++sig) {
        if ((*sig >= '0' && *sig <= '9') || *sig == '?')
            continue;
        if (count == sizeof(values) / sizeof(values[0]))
            return NULL;
        switch (*sig) {
        case 'i': values[count].i = va_arg(args, int); break;
        case 'u': values[count].u = va_arg(args, unsigned int); break;
        case 'f': values[count].f = va_arg(args, wl_fixed_t); break;
        case 's': values[count].s = va_arg(args, const char*); break;
        case 'o': case 'n': values[count].o = va_arg(args, void*); break;
        case 'a': values[count].a = va_arg(args, struct wl_array*); break;
        case 'h': values[count].h = va_arg(args, int); break;
        default: return NULL;
        }
        ++count;
    }
    return marshal_array(proxy, opcode, interface, version, flags, values);
}

struct wl_proxy* wl_proxy_marshal_flags(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, uint32_t version, uint32_t flags, ...)
{
    va_list args;
    va_start(args, flags);
    struct wl_proxy* result = marshal_va(proxy, opcode, interface, version, flags, args);
    va_end(args);
    return result;
}

struct wl_proxy* wl_proxy_marshal_array_flags(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, uint32_t version, uint32_t flags, union wl_argument* args)
{
    return marshal_array(proxy, opcode, interface, version, flags, args);
}

void wl_proxy_marshal(struct wl_proxy* proxy, uint32_t opcode, ...)
{
    va_list args;
    va_start(args, opcode);
    marshal_va(proxy, opcode, NULL, proxy_version(proxy), 0, args);
    va_end(args);
}

void wl_proxy_marshal_array(struct wl_proxy* proxy, uint32_t opcode, union wl_argument* args)
{
    marshal_array(proxy, opcode, NULL, proxy_version(proxy), 0, args);
}

struct wl_proxy* wl_proxy_marshal_constructor(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, ...)
{
    va_list args;
    va_start(args, interface);
    struct wl_proxy* result = marshal_va(proxy, opcode, interface, proxy_version(proxy), 0, args);
    va_end(args);
    return result;
}

struct wl_proxy* wl_proxy_marshal_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode,
    const struct wl_interface* interface, uint32_t version, ...)
{
    va_list args;
    va_start(args, version);
    struct wl_proxy* result = marshal_va(proxy, opcode, interface, version, 0, args);
    va_end(args);
    return result;
}

struct wl_proxy* wl_proxy_marshal_array_constructor(struct wl_proxy* proxy, uint32_t opcode,
    union wl_argument* args, const struct wl_interface* interface)
{
    return marshal_array(proxy, opcode, interface, proxy_version(proxy), 0, args);
}

struct wl_proxy* wl_proxy_marshal_array_constructor_versioned(struct wl_proxy* proxy, uint32_t opcode,
    union wl_argument* args, const struct wl_interface* interface, uint32_t version)
{
    return marshal_array(proxy, opcode, interface, version, 0, args);
}

#define FORWARD(ret, name, params, args) \
    ret name params { \
        ret (*real) params = color_hook("mangohud_" #name); \
        if (!real) real = real_dlsym(RTLD_NEXT, #name); \
        return real args; \
    }
FORWARD(void, wl_proxy_destroy, (struct wl_proxy* proxy), (proxy))
FORWARD(void*, wl_proxy_create_wrapper, (void* proxy), (proxy))
FORWARD(void, wl_proxy_wrapper_destroy, (void* proxy), (proxy))
FORWARD(void, wl_display_disconnect, (struct wl_display* display), (display))
#undef FORWARD

void* mangohud_find_wayland_ptr(const char* name);
void* mangohud_find_wayland_ptr(const char* name)
{
#define HOOK(fn) if (!strcmp(name, #fn)) return (void*)fn
    HOOK(wl_proxy_marshal_flags);
    HOOK(wl_proxy_marshal_array_flags);
    HOOK(wl_proxy_marshal);
    HOOK(wl_proxy_marshal_array);
    HOOK(wl_proxy_marshal_constructor);
    HOOK(wl_proxy_marshal_constructor_versioned);
    HOOK(wl_proxy_marshal_array_constructor);
    HOOK(wl_proxy_marshal_array_constructor_versioned);
    HOOK(wl_proxy_destroy);
    HOOK(wl_proxy_create_wrapper);
    HOOK(wl_proxy_wrapper_destroy);
    HOOK(wl_display_disconnect);
#undef HOOK
    return NULL;
}
#endif
