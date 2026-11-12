#include "../wayland_color.h"
#include "../../mangohud-next/client/hooks_helper.h"
#include "../../mangohud-next/server/common/helpers.hpp"
#include <string>
#include <mutex>

namespace {
struct Proxy {
    wl_proxy* proxy = nullptr;
    wl_display* display = nullptr;
    wl_proxy* original = nullptr;
};
struct State {
    std::recursive_mutex mutex;
    WaylandColorTracker colors;
    Collection<Proxy> proxies;

    wl_proxy* original(wl_proxy* proxy) {
        auto item = proxies.fetch(&Proxy::proxy, proxy);
        return item && item->original ? item->original : proxy;
    }

    wl_display* display(wl_proxy* proxy) {
        auto item = proxies.fetch(&Proxy::proxy, proxy);
        return item ? item->display : nullptr;
    }

    void track(wl_proxy* proxy, wl_display* display, wl_proxy* original = nullptr) {
        proxies.remove(&Proxy::proxy, proxy);
        proxies.add(std::make_shared<Proxy>(Proxy{proxy, display, original}));
    }
};
State& state() {
    static auto* value = new State;
    return *value;
}

wl_proxy* marshal_wayland(wl_proxy* proxy, uint32_t opcode,
                          const wl_interface* interface, uint32_t version,
                          uint32_t flags, wl_argument* args)
{
    auto real = wl_marshal_real_array_flags();
    if (!real)
        return nullptr;
    auto& s = state();
    std::lock_guard lock(s.mutex);
    const auto* source = wl_proxy_get_interface(proxy);
    const char* name = wl_proxy_get_class(proxy);
    const std::string cls = name ? name : "";
    const std::string request = source && opcode < static_cast<uint32_t>(source->method_count)
        ? source->methods[opcode].name : "";
    auto* original = s.original(proxy);
    auto* display = cls == "wl_display" ? reinterpret_cast<wl_display*>(original) : s.display(proxy);
    std::array<wl_argument, 20> observed{};
    const bool relevant = cls == "wp_color_manager_v1" ||
        cls == "wp_image_description_creator_params_v1" ||
        cls == "wp_color_management_surface_v1" ||
        cls == "wp_image_description_v1" || cls == "wl_surface";
    if (relevant && args) {
        if (cls == "wp_color_manager_v1" && request == "get_surface") {
            observed[1].o = reinterpret_cast<wl_object*>(s.original(reinterpret_cast<wl_proxy*>(args[1].o)));
        } else {
            observed[0] = args[0];
            if (cls == "wp_color_management_surface_v1" && request == "set_image_description")
                observed[0].o = reinterpret_cast<wl_object*>(s.original(reinterpret_cast<wl_proxy*>(args[0].o)));
        }
    }
    auto* result = real(proxy, opcode, interface, version, flags, args);
    if (result)
        s.track(result, display);
    if (relevant)
        s.colors.observe(cls, request, original, result, observed.data());
    if (flags & WL_MARSHAL_FLAG_DESTROY) {
        s.colors.forget(original);
        s.proxies.remove(&Proxy::proxy, proxy);
    }
    return result;
}
}

static wl_proxy* marshal_wayland_va(wl_proxy* proxy, uint32_t opcode,
                                    const wl_interface* interface, uint32_t version,
                                    uint32_t flags, va_list args_in)
{
    const auto* proxy_interface = wl_proxy_get_interface(proxy);
    const wl_message* message = nullptr;
    if (proxy_interface && opcode < static_cast<uint32_t>(proxy_interface->method_count))
        message = &proxy_interface->methods[opcode];
    std::array<wl_argument, 20> args;
    auto count = wl_marshal_arguments_from_va_list(message, args_in, args.data(), args.size());
    return marshal_wayland(proxy, opcode, interface, version, flags, count ? args.data() : nullptr);
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_flags(wl_proxy* proxy, uint32_t opcode,
                                            const wl_interface* interface,
                                            uint32_t version, uint32_t flags, ...)
{
    va_list args;
    va_start(args, flags);
    auto* result = marshal_wayland_va(proxy, opcode, interface, version, flags, args);
    va_end(args);
    return result;
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_array_flags(wl_proxy* proxy, uint32_t opcode,
                                                  const wl_interface* interface,
                                                  uint32_t version, uint32_t flags,
                                                  wl_argument* args)
{
    return marshal_wayland(proxy, opcode, interface, version, flags, args);
}

EXPORT_C_(void) wl_proxy_marshal(wl_proxy* proxy, uint32_t opcode, ...)
{
    va_list args;
    va_start(args, opcode);
    marshal_wayland_va(proxy, opcode, nullptr, wl_proxy_get_version(proxy), 0, args);
    va_end(args);
}

EXPORT_C_(void) wl_proxy_marshal_array(wl_proxy* proxy, uint32_t opcode, wl_argument* args)
{
    marshal_wayland(proxy, opcode, nullptr, wl_proxy_get_version(proxy), 0, args);
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_constructor(wl_proxy* proxy, uint32_t opcode,
                                                  const wl_interface* interface, ...)
{
    va_list args;
    va_start(args, interface);
    auto* result = marshal_wayland_va(proxy, opcode, interface, wl_proxy_get_version(proxy), 0, args);
    va_end(args);
    return result;
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_constructor_versioned(wl_proxy* proxy, uint32_t opcode,
    const wl_interface* interface, uint32_t version, ...)
{
    va_list args;
    va_start(args, version);
    auto* result = marshal_wayland_va(proxy, opcode, interface, version, 0, args);
    va_end(args);
    return result;
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_array_constructor(wl_proxy* proxy, uint32_t opcode,
    wl_argument* args, const wl_interface* interface)
{
    return marshal_wayland(proxy, opcode, interface, wl_proxy_get_version(proxy), 0, args);
}

EXPORT_C_(wl_proxy*) wl_proxy_marshal_array_constructor_versioned(wl_proxy* proxy, uint32_t opcode,
    wl_argument* args, const wl_interface* interface, uint32_t version)
{
    return marshal_wayland(proxy, opcode, interface, version, 0, args);
}


EXPORT_C_(void) wl_proxy_destroy(wl_proxy* proxy)
{
    static auto real = reinterpret_cast<decltype(&wl_proxy_destroy)>(real_dlsym(RTLD_NEXT, "wl_proxy_destroy"));
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.original(proxy) == proxy)
        s.colors.forget(proxy);
    s.proxies.remove(&Proxy::proxy, proxy);
    real(proxy);
}

EXPORT_C_(void*) wl_proxy_create_wrapper(void* proxy)
{
    static auto real = reinterpret_cast<decltype(&wl_proxy_create_wrapper)>(real_dlsym(RTLD_NEXT, "wl_proxy_create_wrapper"));
    auto& s = state();
    std::lock_guard lock(s.mutex);
    auto* wrapper = static_cast<wl_proxy*>(real(proxy));
    auto* original = s.original(static_cast<wl_proxy*>(proxy));
    if (wrapper) {
        const char* cls = wl_proxy_get_class(original);
        auto* display = cls && strcmp(cls, "wl_display") == 0
            ? reinterpret_cast<wl_display*>(original) : s.display(original);
        s.track(wrapper, display, original);
    }
    return wrapper;
}

EXPORT_C_(void) wl_proxy_wrapper_destroy(void* proxy)
{
    static auto real = reinterpret_cast<decltype(&wl_proxy_wrapper_destroy)>(real_dlsym(RTLD_NEXT, "wl_proxy_wrapper_destroy"));
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.proxies.remove(&Proxy::proxy, static_cast<wl_proxy*>(proxy));
    real(proxy);
}

EXPORT_C_(void) wl_display_disconnect(wl_display* display)
{
    static auto real = reinterpret_cast<decltype(&wl_display_disconnect)>(real_dlsym(RTLD_NEXT, "wl_display_disconnect"));
    auto& s = state();
    std::lock_guard lock(s.mutex);
    while (auto item = s.proxies.fetch_any(&Proxy::display, display)) {
        s.colors.forget(item->proxy);
        s.proxies.remove(&Proxy::proxy, item->proxy);
    }
    auto* proxy = reinterpret_cast<wl_proxy*>(display);
    s.colors.forget(proxy);
    s.proxies.remove(&Proxy::proxy, proxy);
    real(display);
}

EXPORT_C_(int) mangohud_wayland_surface_color(const void* surface)
{
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return static_cast<int>(s.colors.color(surface));
}

EXPORT_C_(void*) mangohud_find_wayland_ptr(const char* name)
{
#define HOOK(fn) if (strcmp(name, #fn) == 0) return reinterpret_cast<void*>(fn)
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
    return nullptr;
}
