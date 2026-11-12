#pragma once

#include <string_view>
#include <unordered_map>
#include <wayland-client.h>

#include "../mangohud-next/render/colors.h"

extern "C" int mangohud_wayland_surface_color(const void* surface);
extern "C" void* mangohud_find_wayland_ptr(const char* name);

class WaylandColorTracker {
    struct Description {
        overlay_transfer_function color = NONE;
        unsigned primaries = 0;
    };
    struct Surface {
        overlay_transfer_function current = NONE;
        overlay_transfer_function pending = NONE;
        bool changed = false;
    };
    std::unordered_map<const void*, Description> creators;
    std::unordered_map<const void*, overlay_transfer_function> descriptions;
    std::unordered_map<const void*, const void*> managed;
    std::unordered_map<const void*, Surface> surfaces;

public:
    overlay_transfer_function color(const void* surface) const {
        auto it = surfaces.find(surface);
        if (it == surfaces.end())
            return NONE;
        return it->second.changed ? it->second.pending : it->second.current;
    }

    void forget(const void* proxy) {
        creators.erase(proxy);
        descriptions.erase(proxy);
        auto it = managed.find(proxy);
        if (it != managed.end()) {
            auto surface = surfaces.find(it->second);
            if (surface != surfaces.end()) {
                surface->second.pending = NONE;
                surface->second.changed = true;
            }
            managed.erase(it);
        }
        surfaces.erase(proxy);
        for (auto i = managed.begin(); i != managed.end();) {
            if (i->second == proxy)
                i = managed.erase(i);
            else
                ++i;
        }
    }

    void observe(std::string_view iface, std::string_view request,
                 const void* proxy, const void* result, const wl_argument* args) {
        if (iface == "wp_color_manager_v1") {
            if (request == "get_surface" && result && args)
                managed[result] = args[1].o;
            else if (request == "create_parametric_creator" && result)
                creators[result] = {};
            else if (request == "create_windows_scrgb" && result)
                descriptions[result] = SRGB;
            else if (request == "create_windows_bt2100" && result)
                descriptions[result] = PQ;
        } else if (iface == "wp_image_description_creator_params_v1") {
            auto& description = creators[proxy];
            if (request == "set_tf_named" && args) {
                switch (args[0].u) {
                case 5: description.color = SRGB; break;
                case 9: description.color = NONE; break;
                case 11: description.color = PQ; break;
                case 13: description.color = HLG; break;
                default: description.color = NONE; break;
                }
            } else if (request == "set_tf_power") {
                description.color = NONE;
            } else if (request == "set_primaries_named" && args) {
                description.primaries = args[0].u;
            } else if (request == "set_primaries") {
                description.primaries = 0;
            } else if (request == "create" && result) {
                const bool supported =
                    (description.color == SRGB && description.primaries == 1) ||
                    ((description.color == PQ || description.color == HLG) && description.primaries == 6);
                descriptions[result] = supported ? description.color : NONE;
                creators.erase(proxy);
            }
        } else if (iface == "wp_color_management_surface_v1") {
            auto it = managed.find(proxy);
            if (it == managed.end())
                return;
            auto& surface = surfaces[it->second];
            if (request == "set_image_description" && args) {
                auto description = descriptions.find(args[0].o);
                surface.pending = description == descriptions.end() ? NONE : description->second;
                surface.changed = true;
            } else if (request == "unset_image_description") {
                surface.pending = NONE;
                surface.changed = true;
            }
        } else if (iface == "wl_surface" && request == "commit") {
            auto it = surfaces.find(proxy);
            if (it != surfaces.end() && it->second.changed) {
                it->second.current = it->second.pending;
                it->second.changed = false;
            }
        }
        if (request == "destroy")
            forget(proxy);
    }
};
