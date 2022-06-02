#pragma once
#ifndef MANGOHUD_KEYBINDS_H
#define MANGOHUD_KEYBINDS_H
#include <vector>

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#else
typedef uint32_t xkb_keysym_t;
#endif

struct wsi_connection;
struct overlay_params;

void check_keybinds(wsi_connection&, overlay_params& params);
bool keys_are_pressed(const std::vector<xkb_keysym_t>& keys);
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
bool wl_keys_are_pressed(const std::vector<xkb_keysym_t>& keys);
void wl_key_pressed(const xkb_keysym_t key, uint32_t state);
#endif
#endif //MANGOHUD_KEYBINDS_H
