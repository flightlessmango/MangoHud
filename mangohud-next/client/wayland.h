#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>
#include <vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../server/common/helpers.hpp"
#include "fractional-scale-v1-client-protocol.h"
#include "ipc.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

struct shm_buffer {
    wl_buffer* buffer = nullptr;
    zwp_linux_buffer_params_v1* params = nullptr;

    IPCClient* ipc = nullptr;
    int idx = -1;
    unique_fd dmabuf_fd;
    int modifier = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int offset = 0;
    std::atomic<bool> busy{false};

    ~shm_buffer() {
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
        }
    }
};

struct surface_data {
    ~surface_data() {
        std::lock_guard lock(m);
        buffers.clear();
        if (fractional_scale)
            wp_fractional_scale_v1_destroy(fractional_scale);
        if (viewport)
            wp_viewport_destroy(viewport);
        if (sub_surf)
            wl_subsurface_destroy(sub_surf);
        if (overlay_surf)
            wl_surface_destroy(overlay_surf);
    }

    wl_display* display = nullptr;
    wl_surface* surface = nullptr;
    wl_event_queue* queue = nullptr;
    wl_surface* overlay_surf = nullptr;
    wl_subsurface* sub_surf = nullptr;
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fractional_scale = nullptr;
    std::atomic<uint32_t> preferred_scale{120};
    bool attached = false;

    std::vector<std::shared_ptr<shm_buffer>> buffers;

    std::mutex m;
};

struct wl_globals {
    wl_event_queue* queue = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    zwp_linux_dmabuf_v1* dmabuf = nullptr;
    wp_viewporter* viewporter = nullptr;
    wp_fractional_scale_manager_v1* fractional_scale_manager = nullptr;

    uint32_t compositor_name = 0;
    uint32_t subcompositor_name = 0;
    uint32_t dmabuf_name = 0;
    uint32_t viewporter_name = 0;
    uint32_t fractional_scale_manager_name = 0;
};

class Wayland {
public:
    Wayland(std::shared_ptr<IPCClient> ipc_) : ipc(std::move(ipc_)) {
        if (ipc) ipc->start(4);
    }

    ~Wayland() {
        quit.store(true);
        if (thread.joinable())
            thread.join();
    }

    template <typename Surface>
    void add_surface(Surface key, wl_surface* wl_surface, wl_display* display) {
        std::lock_guard lock(surf_m);
        auto surf_data = std::make_shared<surface_data>();
        surf_data->display = display;
        surf_data->surface = wl_surface;
        if constexpr (std::is_same_v<Surface, VkSurfaceKHR>) {
            surfaces[key] = std::move(surf_data);
        } else {
            static_assert(std::is_same_v<Surface, EGLSurface>);
            egl_surfaces[key] = std::move(surf_data);
        }
    }

    template <typename Surface>
    void destroy_surface(Surface key) {
        std::lock_guard lock(surf_m);
        if constexpr (std::is_same_v<Surface, VkSurfaceKHR>) {
            surfaces.erase(key);
        } else {
            static_assert(std::is_same_v<Surface, EGLSurface>);
            egl_surfaces.erase(key);
        }
    }

    void destroy_egl_display_surfaces(wl_display* display) {
        std::lock_guard lock(surf_m);
        for (auto it = egl_surfaces.begin(); it != egl_surfaces.end();) {
            if (it->second && it->second->display == display)
                it = egl_surfaces.erase(it);
            else
                ++it;
        }
    }

    template <typename Surface>
    bool ensure_overlay(Surface surface) {
        if (!ensure_overlay_data(get_surface(surface))) return false;
        if (!thread.joinable()) thread = std::thread([this, surface] { run_thread(surface); });
        return true;
    }

private:
    template <typename Surface>
    std::shared_ptr<surface_data> get_surface(Surface key) {
        std::lock_guard lock(surf_m);
        if constexpr (std::is_same_v<Surface, VkSurfaceKHR>) {
            auto it = surfaces.find(key);
            return it != surfaces.end() ? it->second : nullptr;
        } else {
            static_assert(std::is_same_v<Surface, EGLSurface>);
            auto it = egl_surfaces.find(key);
            return it != egl_surfaces.end() ? it->second : nullptr;
        }
    }

    wl_globals& get_global(wl_display* display);

    std::unordered_map<VkSurfaceKHR, std::shared_ptr<surface_data>> surfaces;
    std::unordered_map<EGLSurface, std::shared_ptr<surface_data>> egl_surfaces;
    std::mutex surf_m;
    std::unordered_map<wl_display*, wl_globals> globals;
    std::mutex globals_m;
    std::shared_ptr<IPCClient> ipc;
    Fdinfo fdinfo;
    uint64_t imported_generation = 0;

    std::thread thread;
    std::atomic<bool> quit{false};

    static void on_registry_global(void* data, wl_registry* registry, uint32_t name,
                                   const char* interface, uint32_t version);
    static void on_registry_global_remove(void* data, wl_registry* registry, uint32_t name);
    static void on_preferred_scale(void* data, wp_fractional_scale_v1*, uint32_t scale);
    static void release_to_server(shm_buffer* buf);
    static void buffer_release(void* data, wl_buffer* = nullptr);

    inline static const wl_registry_listener registry_listener = {
        Wayland::on_registry_global,
        Wayland::on_registry_global_remove,
    };

    inline static const wl_buffer_listener buffer_listener = {
        .release = Wayland::buffer_release,
    };

    inline static const wp_fractional_scale_v1_listener fractional_scale_listener = {
        .preferred_scale = Wayland::on_preferred_scale,
    };

    bool ensure_overlay_data(const std::shared_ptr<surface_data>& surf_data);
    void update_import(const std::shared_ptr<surface_data>& surf_data);
    void dispatch_events(const std::shared_ptr<surface_data>& surf_data);
    std::shared_ptr<shm_buffer> present(const std::shared_ptr<surface_data>& surf_data);
    void detach(const std::shared_ptr<surface_data>& surf_data, bool wait_for_server = true);

    template <typename Surface>
    void run_thread(Surface surface) {
        while (!quit.load()) {
            {
                auto surf_data = get_surface(surface);
                dispatch_events(surf_data);
                if (!ipc->connected.load(std::memory_order_acquire)) {
                    detach(surf_data);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } else {
                    update_import(surf_data);
                    present(surf_data);
                    dispatch_events(surf_data);
                }

                if (quit.load())
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        detach(get_surface(surface), false);
    }
};
