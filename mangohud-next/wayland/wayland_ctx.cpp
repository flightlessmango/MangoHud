#include "wayland_ctx.h"

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

wl_globals* WaylandCtx::get_global(wl_display* display)
{
    if (!display)
        return nullptr;

    std::lock_guard lock(globals_m);
    auto& global = globals[display];
    global.display = display;
    global.ctx = this;

    if (!global.registry) {
        if (!global.queue)
            global.queue = wl_display_create_queue(display);
        if (!global.queue)
            return nullptr;

        auto* wrapped_display = reinterpret_cast<wl_display*>(wl_proxy_create_wrapper(display));
        if (!wrapped_display)
            return nullptr;

        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped_display), global.queue);

        global.registry = wl_display_get_registry(wrapped_display);
        wl_proxy_wrapper_destroy(wrapped_display);
        if (!global.registry)
            return nullptr;

        if (wl_registry_add_listener(global.registry, &registry_listener, &global) != 0) {
            wl_registry_destroy(global.registry);
            global.registry = nullptr;
            return nullptr;
        }

        if (wl_display_roundtrip_queue(display, global.queue) < 0)
            return nullptr;

        global.initialized = true;
    }

    return global.initialized ? &global : nullptr;
}

void WaylandCtx::on_registry_global(void* data, wl_registry* registry, uint32_t name,
                                    const char* interface, uint32_t version)
{
    auto& global = *reinterpret_cast<wl_globals*>(data);

    uint32_t compositor_version = std::min(version, 4u);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        global.compositor = reinterpret_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, compositor_version));
        if (!global.compositor)
            return;
        global.compositor_name = name;
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        global.subcompositor = reinterpret_cast<wl_subcompositor*>(
            wl_registry_bind(registry, name, &wl_subcompositor_interface, compositor_version));
        if (!global.subcompositor)
            return;
        global.subcompositor_name = name;
    } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        global.dmabuf = reinterpret_cast<zwp_linux_dmabuf_v1*>(
            wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3));
        if (!global.dmabuf)
            return;
        global.dmabuf_name = name;
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        global.viewporter = reinterpret_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
        if (!global.viewporter)
            return;
        global.viewporter_name = name;
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        global.fractional_scale_manager = reinterpret_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
        if (!global.fractional_scale_manager)
            return;
        global.fractional_scale_manager_name = name;
    } else if (strcmp(interface, wp_presentation_interface.name) == 0) {
        global.presentation = reinterpret_cast<wp_presentation*>(
            wl_registry_bind(registry, name, &wp_presentation_interface, std::min(version, 2u)));
        if (!global.presentation)
            return;
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(global.presentation), global.queue);
        global.presentation_name = name;
        if (wp_presentation_add_listener(global.presentation, &presentation_listener, &global) != 0) {
            wp_presentation_destroy(global.presentation);
            global.presentation = nullptr;
            global.presentation_name = 0;
        }
    } else if (global.ctx && global.ctx->listener.global) {
        global.ctx->listener.global(global.ctx->listener.data, global, registry, name, interface, version);
    }
}

void WaylandCtx::on_registry_global_remove(void* data, wl_registry*, uint32_t name)
{
    auto& global = *reinterpret_cast<wl_globals*>(data);

    if (name == global.compositor_name) {
        wl_compositor_destroy(global.compositor);
        global.compositor = nullptr;
        global.compositor_name = 0;
    } else if (name == global.subcompositor_name) {
        wl_subcompositor_destroy(global.subcompositor);
        global.subcompositor = nullptr;
        global.subcompositor_name = 0;
    } else if (name == global.viewporter_name) {
        wp_viewporter_destroy(global.viewporter);
        global.viewporter = nullptr;
        global.viewporter_name = 0;
    } else if (name == global.fractional_scale_manager_name) {
        wp_fractional_scale_manager_v1_destroy(global.fractional_scale_manager);
        global.fractional_scale_manager = nullptr;
        global.fractional_scale_manager_name = 0;
    } else if (name == global.presentation_name) {
        wp_presentation_destroy(global.presentation);
        global.presentation = nullptr;
        global.presentation_name = 0;
        global.presentation_clock_id = 0;
        global.have_presentation_clock_id = false;
    }

    if (global.ctx && global.ctx->listener.global_remove)
        global.ctx->listener.global_remove(global.ctx->listener.data, global, name);
}

void WaylandCtx::on_presentation_clock_id(void* data, wp_presentation*, uint32_t clock_id)
{
    auto* global = reinterpret_cast<wl_globals*>(data);
    if (!global)
        return;

    global->presentation_clock_id = clock_id;
    global->have_presentation_clock_id = true;
    SPDLOG_DEBUG("wl presentation clock id: {}", clock_id);
}
