#pragma once

#include <wayland-client.h>
#include <set>
#include <vector>
#include "wayland_color.h"

#ifndef KeySym
typedef unsigned long KeySym;
#endif

extern void* wl_handle;

bool has_wayland_display(struct wl_display *display);
bool wayland_has_keys_pressed(const std::vector<KeySym>& keys);
void init_wayland_data(struct wl_display *display, void *vk_surface, struct wl_surface* surface = nullptr);
overlay_transfer_function wayland_surface_color(void* vk_surface);
void wayland_data_unref(struct wl_display *display, void *vk_surface);
void update_wl_queue();
