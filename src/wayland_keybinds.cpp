#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include "wayland_hook.h"
#include "timing.hpp"
#include "keybinds.h"

void* wl_handle = nullptr;
struct wl_display* wl_display_ptr = nullptr;
struct wl_seat* seat = nullptr;
struct wl_keyboard* keyboard = nullptr;
struct xkb_context *context_xkb = nullptr;
struct xkb_keymap *keymap_xkb = nullptr;
struct xkb_state *state_xkb = nullptr;
struct wl_event_queue* queue = nullptr;
std::vector<KeySym> wl_pressed_keys {};

static void seat_handle_capabilities(void *data, wl_seat *seat, uint32_t caps);
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {}

struct wl_seat_listener seat_listener {
   .capabilities = seat_handle_capabilities,
   .name = seat_handle_name,
};

static void registry_handle_global(void *data, struct wl_registry* registry, uint32_t name, const char *interface, uint32_t version)
{
   if(strcmp(interface, wl_seat_interface.name) == 0)
   {
      seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 5);
      wl_seat_add_listener(seat, &seat_listener, NULL);
   }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name){}

static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size)
{
   char* map_shm = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

   if(!context_xkb)
      context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

   if(keymap_xkb && state_xkb)
   {
      xkb_keymap_unref(keymap_xkb);
      xkb_state_unref(state_xkb);
   }

   keymap_xkb = xkb_keymap_new_from_string(
        context_xkb, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

   state_xkb = xkb_state_new(keymap_xkb);

   munmap((void*)map_shm, size);
   close(fd);
}

static void wl_keyboard_enter(void *user_data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys){}

static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface)
{
   wl_pressed_keys.clear();
}

static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
   xkb_keycode_t keycode = key + 8;
   xkb_keysym_t keysym = xkb_state_key_get_one_sym(state_xkb, keycode);

   if(state)
   {
      wl_pressed_keys.push_back(keysym);
   }
   else
   {
      auto it = std::find(wl_pressed_keys.begin(), wl_pressed_keys.end(), keysym);
      if(it != wl_pressed_keys.end())
         wl_pressed_keys.erase(it);
   }
}

static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group){}

static void wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay){}

struct wl_registry_listener registry_listener {
   .global = registry_handle_global,
   .global_remove = registry_handle_global_remove
};

struct wl_keyboard_listener keyboard_listener {
   .keymap = wl_keyboard_keymap,
   .enter = wl_keyboard_enter,
   .leave = wl_keyboard_leave,
   .key = wl_keyboard_key,
   .modifiers = wl_keyboard_modifiers,
   .repeat_info = wl_keyboard_repeat_info
};

static void seat_handle_capabilities(void *data, wl_seat *seat, uint32_t caps)
{
   if(caps & WL_SEAT_CAPABILITY_KEYBOARD)
   {
      if(!keyboard)
      {
         keyboard = wl_seat_get_keyboard(seat);
         wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
      }
   }
}

void update_wl_queue()
{
   wl_display_dispatch_queue_pending(wl_display_ptr, queue);
}

void init_wayland_data()
{
   if (!wl_display_ptr)
      return;

   queue = wl_display_create_queue(wl_display_ptr);
   struct wl_display *display_wrapped = (struct wl_display*)wl_proxy_create_wrapper(wl_display_ptr);
   wl_proxy_set_queue((struct wl_proxy*)display_wrapped, queue);
   wl_registry *registry = wl_display_get_registry(display_wrapped);
   wl_proxy_wrapper_destroy(display_wrapped);
   wl_registry_add_listener(registry, &registry_listener, NULL);
   wl_display_roundtrip_queue(wl_display_ptr, queue);
   wl_display_roundtrip_queue(wl_display_ptr, queue);
}
