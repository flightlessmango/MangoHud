#include <wayland-client.h>
#include <set>
#include <vector>

#ifndef KeySym
typedef unsigned long KeySym;
#endif

extern void* wl_handle;
extern struct wl_display* wl_display_ptr;
extern std::set<KeySym> wl_pressed_keys;

void init_wayland_data();
void update_wl_queue();
