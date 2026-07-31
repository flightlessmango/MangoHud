#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>
#include <vulkan.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../server/common/helpers.hpp"
#include "ipc.h"
#include "../wayland/wayland_ctx.h"

class Wayland;

struct shm_buffer {
    wl_buffer* buffer = nullptr;

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

struct presentation_feedback_state {
    std::atomic<bool> output_pending{false};
    std::atomic<bool> hud_pending{false};
    std::atomic<uint64_t> hud_seq{0};

    std::atomic<bool>& pending(SampleType type) {
        return type == SampleType::Hud ? hud_pending : output_pending;
    }

    uint64_t next_hud_seq() {
        return hud_seq.fetch_add(1, std::memory_order_relaxed);
    }
};

struct focus_signal_state {
    bool active = false;
    uint64_t time_ns = 0;
};

struct focus_state {
    focus_signal_state keyboard;
    // TODO: wire wl_pointer enter/leave as a focus tie-breaker.
    focus_signal_state pointer;
    focus_signal_state presentation;

    bool focused() const {
        return presentation.active && (keyboard.active || pointer.active);
    }
};

struct seat_focus_snapshot {
    std::string identifier;
    focus_state focus;
};

struct seat_data {
    static constexpr uint64_t presentation_timeout_ns = 1000000000ULL;

    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_event_queue* queue = nullptr;
    wl_display* display = nullptr;
    uint32_t registry_name = 0;
    std::string seat_name;
    focus_state focus;
    mutable std::mutex state_m;

    seat_focus_snapshot update(uint64_t now) {
        std::lock_guard lock(state_m);
        auto& presentation = focus.presentation;
        if (presentation.active &&
            presentation.time_ns != 0 &&
            now - presentation.time_ns > presentation_timeout_ns) {
            presentation.active = false;
        }

        return make_snapshot();
    }

    seat_focus_snapshot set_keyboard(bool active, uint64_t time_ns) {
        std::lock_guard lock(state_m);
        focus.keyboard.active = active;
        focus.keyboard.time_ns = time_ns;
        return make_snapshot();
    }

    void set_presentation(bool active, uint64_t time_ns) {
        std::lock_guard lock(state_m);
        focus.presentation.active = active;
        focus.presentation.time_ns = time_ns;
    }

    seat_focus_snapshot set_name(const char* name) {
        std::lock_guard lock(state_m);
        seat_name = name ? name : "";
        return make_snapshot();
    }

    std::string identifier() const {
        std::lock_guard lock(state_m);
        return current_identifier();
    }

    seat_focus_snapshot snapshot() const {
        std::lock_guard lock(state_m);
        return make_snapshot();
    }

    void release_keyboard() {
        if (!keyboard)
            return;

        if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(keyboard);
        else
            wl_keyboard_destroy(keyboard);
        keyboard = nullptr;
    }

    void destroy() {
        release_keyboard();
        if (seat) {
            if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION)
                wl_seat_release(seat);
            else
                wl_seat_destroy(seat);
            seat = nullptr;
        }
    }

private:
    std::string current_identifier() const {
        return !seat_name.empty() ? seat_name : std::to_string(registry_name);
    }

    seat_focus_snapshot make_snapshot() const {
        return {current_identifier(), focus};
    }
};

struct surface_data {
    void destroy_wayland_objects() {
        std::lock_guard lock(m);
        buffers.clear();
        if (fractional_scale) {
            wp_fractional_scale_v1_destroy(fractional_scale);
            fractional_scale = nullptr;
        }
        if (viewport) {
            wp_viewport_destroy(viewport);
            viewport = nullptr;
        }
        if (sub_surf) {
            wl_subsurface_destroy(sub_surf);
            sub_surf = nullptr;
        }
        if (overlay_surf) {
            wl_surface_destroy(overlay_surf);
            overlay_surf = nullptr;
        }
        attached = false;
    }

    wl_display* display = nullptr;
    wl_surface* surface = nullptr;
    wl_event_queue* queue = nullptr;
    wl_surface* overlay_surf = nullptr;
    wl_subsurface* sub_surf = nullptr;
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fractional_scale = nullptr;
    std::atomic<uint32_t> preferred_scale{120};
    std::shared_ptr<presentation_feedback_state> presentation_feedback =
        std::make_shared<presentation_feedback_state>();
    bool attached = false;

    std::vector<std::shared_ptr<shm_buffer>> buffers;

    std::mutex m;
};

struct presentation_feedback_data {
    std::weak_ptr<IPCClient> ipc;
    std::shared_ptr<presentation_feedback_state> feedback_state;
    SampleType type = SampleType::Frame;
    const char* surface = nullptr;
    Wayland* wayland = nullptr;

    void clear_pending() {
        if (feedback_state)
            feedback_state->pending(type).store(false, std::memory_order_release);
    }
};

class Wayland {
public:
    Wayland(std::shared_ptr<IPCClient> ipc_) :
        ctx({this, Wayland::on_global, Wayland::on_global_remove}),
        ipc(std::move(ipc_)) {
        if (ipc) ipc->start(4);
    }

    ~Wayland();

    void destroy_wayland_objects();

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
        std::shared_ptr<surface_data> surf_data;
        {
            std::lock_guard lock(surf_m);
            if constexpr (std::is_same_v<Surface, VkSurfaceKHR>) {
                auto it = surfaces.find(key);
                if (it != surfaces.end()) {
                    surf_data = it->second;
                    surfaces.erase(it);
                }
            } else {
                static_assert(std::is_same_v<Surface, EGLSurface>);
                auto it = egl_surfaces.find(key);
                if (it != egl_surfaces.end()) {
                    surf_data = it->second;
                    egl_surfaces.erase(it);
                }
            }
        }

        if (surf_data == active_surface) {
            quit.store(true);
            if (thread.joinable())
                thread.join();
            active_surface.reset();
        }

        if (surf_data)
            surf_data->destroy_wayland_objects();
    }

    template <typename Surface>
    bool ensure_overlay(Surface surface) {
        auto surf_data = get_surface(surface);
        if (!ensure_overlay_data(surf_data)) return false;
        // TODO: expand this to one worker per surface if we support multiple active overlay surfaces.
        if (!thread.joinable()) {
            active_surface = surf_data;
            quit.store(false);
            thread = std::thread([this, surf_data] { run_thread(surf_data); });
        }
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

    WaylandCtx ctx;
    std::unordered_map<VkSurfaceKHR, std::shared_ptr<surface_data>> surfaces;
    std::unordered_map<EGLSurface, std::shared_ptr<surface_data>> egl_surfaces;
    std::mutex surf_m;
    std::vector<std::unique_ptr<seat_data>> seats;
    std::mutex seats_m;
    std::shared_ptr<IPCClient> ipc;
    Fdinfo fdinfo;
    uint64_t imported_generation = 0;

    std::thread thread;
    std::atomic<bool> quit{false};
    std::shared_ptr<surface_data> active_surface;

    static void on_global(void* data, wl_globals& global, wl_registry* registry,
                          uint32_t name, const char* interface, uint32_t version);
    static void on_global_remove(void* data, wl_globals& global, uint32_t name);
    static void on_seat_capabilities(void* data, wl_seat*, uint32_t capabilities);
    static void on_seat_name(void* data, wl_seat*, const char* name);
    static void on_keyboard_keymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t);
    static void on_keyboard_enter(void* data, wl_keyboard*, uint32_t, wl_surface*, wl_array*);
    static void on_keyboard_leave(void* data, wl_keyboard*, uint32_t, wl_surface*);
    static void on_keyboard_key(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void on_keyboard_modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void on_keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}
    static void on_preferred_scale(void* data, wp_fractional_scale_v1*, uint32_t scale);
    static void on_presentation_feedback_sync_output(void*, struct wp_presentation_feedback*, wl_output*) {}
    static void on_presentation_feedback_presented(void* data, struct wp_presentation_feedback* feedback,
                                                   uint32_t tv_sec_hi, uint32_t tv_sec_lo,
                                                   uint32_t tv_nsec, uint32_t refresh,
                                                   uint32_t seq_hi, uint32_t seq_lo,
                                                   uint32_t flags);
    static void on_presentation_feedback_discarded(void* data, struct wp_presentation_feedback* feedback);
    static void release_to_server(shm_buffer* buf);
    static void buffer_release(void* data, wl_buffer* = nullptr);

    inline static const wl_buffer_listener buffer_listener = {
        .release = Wayland::buffer_release,
    };

    inline static const wl_seat_listener seat_listener = {
        .capabilities = Wayland::on_seat_capabilities,
        .name = Wayland::on_seat_name,
    };

    inline static const wl_keyboard_listener keyboard_listener = {
        .keymap = Wayland::on_keyboard_keymap,
        .enter = Wayland::on_keyboard_enter,
        .leave = Wayland::on_keyboard_leave,
        .key = Wayland::on_keyboard_key,
        .modifiers = Wayland::on_keyboard_modifiers,
        .repeat_info = Wayland::on_keyboard_repeat_info,
    };

    inline static const wp_fractional_scale_v1_listener fractional_scale_listener = {
        .preferred_scale = Wayland::on_preferred_scale,
    };

    inline static const wp_presentation_feedback_listener presentation_feedback_listener = {
        .sync_output = Wayland::on_presentation_feedback_sync_output,
        .presented = Wayland::on_presentation_feedback_presented,
        .discarded = Wayland::on_presentation_feedback_discarded,
    };

    inline static char app_surface_name[] = "app";
    inline static char hud_surface_name[] = "hud";

    bool request_presentation_feedback(const std::shared_ptr<surface_data>& surf_data, wl_globals& globals,
                                       wl_surface* surface, SampleType type, const char* surface_name);
    bool ensure_overlay_data(const std::shared_ptr<surface_data>& surf_data);
    void remove_seat(uint32_t name, wl_display* display);
    void update_import(const std::shared_ptr<surface_data>& surf_data);
    void set_presentation_focus(bool active, uint64_t time_ns);
    void update_focus();
    void dispatch_events(const std::shared_ptr<surface_data>& surf_data);
    std::shared_ptr<shm_buffer> present(const std::shared_ptr<surface_data>& surf_data);
    void detach(const std::shared_ptr<surface_data>& surf_data, bool wait_for_server = true);

    void run_thread(std::shared_ptr<surface_data> surf_data);
};
