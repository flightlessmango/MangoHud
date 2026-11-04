#pragma once

#include <array>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <wayland-client.h>
#include "real_dlsym.h"

using marshal_array_flags_fn = wl_proxy* (*)(wl_proxy*, uint32_t,
                                             const wl_interface*,
                                             uint32_t, uint32_t,
                                             wl_argument*);
using get_interface_fn = const wl_interface* (*)(wl_proxy*);

inline marshal_array_flags_fn wl_marshal_real_array_flags()
{
    static marshal_array_flags_fn real = nullptr;
    if (!real)
        real = reinterpret_cast<marshal_array_flags_fn>(
            real_dlsym(RTLD_NEXT, "wl_proxy_marshal_array_flags"));

    return real;
}

inline bool wl_marshal_is_surface_commit(wl_proxy* proxy, uint32_t opcode)
{
    if (!proxy || opcode != WL_SURFACE_COMMIT)
        return false;

    auto* proxy_class = wl_proxy_get_class(proxy);
    return proxy_class && std::strcmp(proxy_class, wl_surface_interface.name) == 0;
}

inline const wl_interface* wl_marshal_proxy_interface_for(wl_proxy* proxy)
{
    if (!proxy)
        return nullptr;

    static auto* real = reinterpret_cast<get_interface_fn>(
        real_dlsym(RTLD_NEXT, "wl_proxy_get_interface"));
    if (real)
        return real(proxy);

    auto* proxy_class = wl_proxy_get_class(proxy);
    if (!proxy_class)
        return nullptr;

    static constexpr std::array core_interfaces{
        &wl_display_interface,
        &wl_registry_interface,
        &wl_callback_interface,
        &wl_compositor_interface,
        &wl_shm_pool_interface,
        &wl_shm_interface,
        &wl_buffer_interface,
        &wl_data_offer_interface,
        &wl_data_source_interface,
        &wl_data_device_interface,
        &wl_data_device_manager_interface,
        &wl_shell_interface,
        &wl_shell_surface_interface,
        &wl_surface_interface,
        &wl_seat_interface,
        &wl_pointer_interface,
        &wl_keyboard_interface,
        &wl_touch_interface,
        &wl_output_interface,
        &wl_region_interface,
        &wl_subcompositor_interface,
        &wl_subsurface_interface,
    };

    for (const auto* iface : core_interfaces)
        if (iface && iface->name && std::strcmp(proxy_class, iface->name) == 0)
            return iface;

    return nullptr;
}

inline size_t wl_marshal_arguments_from_va_list(const wl_message* message,
                                                va_list args_in,
                                                wl_argument* args,
                                                size_t max_args)
{
    if (!message || !message->signature || !args)
        return 0;

    size_t count = 0;
    for (const char* sig = message->signature; *sig; sig++) {
        if ((*sig >= '0' && *sig <= '9') || *sig == '?')
            continue;
        if (count == max_args)
            break;

        wl_argument arg{};
        switch (*sig) {
        case 'i':
            arg.i = va_arg(args_in, int);
            break;
        case 'u':
            arg.u = va_arg(args_in, unsigned int);
            break;
        case 'f':
            arg.f = va_arg(args_in, wl_fixed_t);
            break;
        case 's':
            arg.s = va_arg(args_in, const char*);
            break;
        case 'o':
        case 'n':
            arg.o = reinterpret_cast<wl_object*>(va_arg(args_in, void*));
            break;
        case 'a':
            arg.a = va_arg(args_in, wl_array*);
            break;
        case 'h':
            arg.h = va_arg(args_in, int);
            break;
        default:
            break;
        }
        args[count++] = arg;
    }

    return count;
}

inline wl_proxy* wl_marshal_forward_flags(marshal_array_flags_fn real,
                                          wl_proxy* proxy, uint32_t opcode,
                                          const wl_interface* interface,
                                          uint32_t version, uint32_t flags,
                                          va_list args_in)
{
    const wl_message* message = nullptr;
    auto* proxy_interface = wl_marshal_proxy_interface_for(proxy);
    if (proxy_interface && opcode < static_cast<uint32_t>(proxy_interface->method_count))
        message = &proxy_interface->methods[opcode];

    std::array<wl_argument, 20> args;
    auto argc = wl_marshal_arguments_from_va_list(message, args_in, args.data(), args.size());
    return real(proxy, opcode, interface, version, flags, argc ? args.data() : nullptr);
}
