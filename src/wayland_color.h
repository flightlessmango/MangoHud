#pragma once

#include "../mangohud-next/client/wayland_color.h"

extern "C" int mangohud_wayland_surface_color(const void* surface);
extern "C" void* mangohud_find_wayland_ptr(const char* name);
