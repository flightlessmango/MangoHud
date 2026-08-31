#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "fractional-scale-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "viewporter-client-protocol.h"

class WaylandCtx;

struct wl_globals {
    wl_event_queue* queue = nullptr;
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    zwp_linux_dmabuf_v1* dmabuf = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;
    wp_presentation* presentation = nullptr;

    uint32_t compositor_name = 0;
    uint32_t subcompositor_name = 0;
    uint32_t dmabuf_name = 0;
    uint32_t viewporter_name = 0;
    uint32_t fractional_scale_manager_name = 0;
    uint32_t presentation_name = 0;
    uint32_t presentation_clock_id = 0;
    bool initialized = false;
    bool have_presentation_clock_id = false;
    WaylandCtx* ctx = nullptr;
};

struct wayland_ctx_listener {
    void* data = nullptr;
    void (*global)(void* data, wl_globals& global, wl_registry* registry,
                   uint32_t name, const char* interface, uint32_t version) = nullptr;
    void (*global_remove)(void* data, wl_globals& global, uint32_t name) = nullptr;
};

class WaylandCtx {
public:
    WaylandCtx() = default;
    explicit WaylandCtx(wayland_ctx_listener listener_) : listener(listener_) {}

    wl_globals* get_global(wl_display* display);

private:
    wayland_ctx_listener listener;
    std::unordered_map<wl_display*, wl_globals> globals;
    std::mutex globals_m;

    static void on_registry_global(void* data, wl_registry* registry, uint32_t name,
                                   const char* interface, uint32_t version);
    static void on_registry_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void on_presentation_clock_id(void* data, wp_presentation*, uint32_t clock_id);

    inline static const wl_registry_listener registry_listener = {
        WaylandCtx::on_registry_global,
        WaylandCtx::on_registry_global_remove,
    };

    inline static const wp_presentation_listener presentation_listener = {
        .clock_id = WaylandCtx::on_presentation_clock_id,
    };
};
