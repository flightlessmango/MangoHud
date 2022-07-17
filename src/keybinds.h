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

typedef std::function<bool(const std::vector<xkb_keysym_t>& keys)> fun_keys_are_pressed;

void check_keybinds(fun_keys_are_pressed, overlay_params& params);
bool keys_are_pressed(const std::vector<xkb_keysym_t>& keys);
#endif //MANGOHUD_KEYBINDS_H
