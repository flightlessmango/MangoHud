#include <wayland-client.h>
#include <set>
#include <vector>

#ifndef KeySym
typedef unsigned long KeySym;
#endif

extern void* wl_handle;

bool has_wayland_display(struct wl_display *display);
bool wayland_has_keys_pressed(const std::vector<KeySym>& keys);
void init_wayland_data(struct wl_display *display, void *vk_surface);
void wayland_data_unref(struct wl_display *display, void *vk_surface);
void update_wl_queue();
