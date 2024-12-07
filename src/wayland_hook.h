#pragma once
#ifndef MANGOHUD_WAYLAND_HOOK_H
#define MANGOHUD_WAYLAND_HOOK_H

#include <vector>

struct wl_display;

#ifndef KeySym
typedef unsigned long KeySym;
#endif

void init_wayland_data(wl_display *display);
void fini_wayland_data();

bool any_wayland_seat_syms_are_pressed(const std::vector<KeySym> &syms);

#endif
