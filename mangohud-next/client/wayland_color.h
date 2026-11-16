#pragma once
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <wayland-client.h>
#include "../render/colors.h"

// Observe application requests without creating a second color-management object
// for its surface or changing the application's event listeners.
class WaylandColorTracker {
    struct Description {
        unsigned transfer = 0;
        unsigned primaries = 0;
    };
    struct SurfaceState {
        Description current;
        Description pending;
        bool changed = false;
    };
    std::mutex m;
    std::unordered_map<const void*, Description> creators;
    std::unordered_map<const void*, Description> descriptions;
    std::unordered_map<const void*, const void*> managed_surfaces;
    std::unordered_map<const void*, SurfaceState> surfaces;

    void forget_locked(const void* proxy) {
        creators.erase(proxy);
        descriptions.erase(proxy);
        auto managed = managed_surfaces.find(proxy);
        if (managed != managed_surfaces.end()) {
            auto surface = surfaces.find(managed->second);
            if (surface != surfaces.end()) {
                surface->second.pending = {};
                surface->second.changed = true;
            }
            managed_surfaces.erase(managed);
        }
        surfaces.erase(proxy);
        std::erase_if(managed_surfaces, [proxy](const auto& item) {
            return item.second == proxy;
        });
    }

public:
    void clear() {
        std::lock_guard lock(m);
        creators.clear();
        descriptions.clear();
        managed_surfaces.clear();
        surfaces.clear();
    }

    void forget(const void* proxy) {
        std::lock_guard lock(m);
        forget_locked(proxy);
    }

    HDR::State status(const void* surface) {
        std::lock_guard lock(m);
        auto it = surfaces.find(surface);
        if (it == surfaces.end())
            return HDR::State::Unknown;

        // ext_linear, st2084_pq and hlg can encode values above white.
        switch (it->second.current.transfer) {
        case 5: case 11: case 13:
            return HDR::State::On;
        case 1: case 2: case 3: case 4: case 9: case 10: case 14:
            return HDR::State::Off;
        default:
            return HDR::State::Unknown;
        }
    }

    overlay_transfer_function color(const void* surface) {
        std::lock_guard lock(m);
        auto it = surfaces.find(surface);
        if (it == surfaces.end())
            return NONE;

        const auto& state = it->second;
        const auto& description = state.changed ? state.pending : state.current;
        if (description.primaries == 1 && description.transfer == 5)
            return SRGB;
        if (description.primaries == 6) {
            if (description.transfer == 11)
                return PQ;
            if (description.transfer == 13)
                return HLG;
        }
        return NONE;
    }

    void observe(std::string_view iface, std::string_view request,
                 const void* proxy, const void* result, const wl_argument* args) {
        std::lock_guard lock(m);
        if (request == "destroy") {
            forget_locked(proxy);
        } else if (iface == "wp_color_manager_v1") {
            if (request == "get_surface" && result && args)
                managed_surfaces[result] = args[1].o;
            else if (request == "create_parametric_creator" && result)
                creators[result] = {};
            else if (request == "create_windows_scrgb" && result)
                descriptions[result] = {5, 1}; // ext_linear, sRGB
            else if (request == "create_windows_bt2100" && result)
                descriptions[result] = {11, 6}; // st2084_pq, BT.2020
        } else if (iface == "wp_image_description_creator_params_v1") {
            if (request == "set_tf_named" && args) {
                creators[proxy].transfer = args[0].u;
            } else if (request == "set_tf_power") {
                creators[proxy].transfer = 0;
            } else if (request == "set_primaries_named" && args) {
                creators[proxy].primaries = args[0].u;
            } else if (request == "set_primaries") {
                creators[proxy].primaries = 0;
            } else if (request == "create") {
                auto it = creators.find(proxy);
                if (result)
                    descriptions[result] = it == creators.end() ? Description{} : it->second;
                creators.erase(proxy);
            }
        } else if (iface == "wp_color_management_surface_v1") {
            auto it = managed_surfaces.find(proxy);
            if (it == managed_surfaces.end())
                return;
            auto& surface = surfaces[it->second];
            if (request == "set_image_description" && args) {
                auto description = descriptions.find(args[0].o);
                surface.pending = description == descriptions.end() ? Description{} : description->second;
                surface.changed = true;
            } else if (request == "unset_image_description") {
                surface.pending = {};
                surface.changed = true;
            }
        } else if (iface == "wl_surface" && request == "commit") {
            auto it = surfaces.find(proxy);
            if (it != surfaces.end() && it->second.changed) {
                it->second.current = it->second.pending;
                it->second.changed = false;
            }
        }
    }
};

inline WaylandColorTracker wayland_color_tracker;
