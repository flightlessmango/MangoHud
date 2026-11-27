#pragma once

#include <cstdarg>
#include "../mangohud-next/client/wayland_color.h"

extern "C" {
int mangohud_wayland_surface_color(const void* surface, int* hdr_status);
wl_proxy* mangohud_wl_marshal_va(wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, va_list);
wl_proxy* mangohud_wl_marshal_array(wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, wl_argument*);
void mangohud_wl_proxy_destroy(wl_proxy*);
void* mangohud_wl_proxy_create_wrapper(void*);
void mangohud_wl_proxy_wrapper_destroy(void*);
void mangohud_wl_display_disconnect(wl_display*);
}
