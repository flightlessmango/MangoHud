#include <cstdint>
#include <array>
#include <dlfcn.h>
#include <cstdio>
#include "real_dlsym.h"
#include "wayland_hook.h"
#include "hud_elements.h"

EXPORT_C_(struct wl_display*) wl_display_connect(const char *name);
EXPORT_C_(struct wl_display*) wl_display_connect_to_fd(int fd);

typedef struct wl_display* (*pwl_display_connect)(const char *name);
typedef struct wl_display* (*pwl_display_connect_to_fd)(int fd);

pwl_display_connect wl_display_connect_ptr = nullptr;
pwl_display_connect_to_fd wl_display_connect_to_fd_ptr = nullptr;
void* wl_handle = nullptr;
struct wl_display* wl_display_ptr = nullptr;

EXPORT_C_(struct wl_display*) wl_display_connect(const char *name)
{
   struct wl_display *ret = nullptr;

   if (!wl_handle) {
      wl_handle = real_dlopen("libwayland-client.so", RTLD_LAZY);
   }

   if (wl_handle) {
       wl_display_connect_ptr = (pwl_display_connect)real_dlsym(wl_handle, "wl_display_connect");
       wl_display_connect_to_fd_ptr = (pwl_display_connect_to_fd)real_dlsym(wl_handle, "wl_display_connect_to_fd");

      ret = wl_display_connect_ptr(name);

      if (!wl_display_ptr) {
         wl_display_ptr = ret;
         HUDElements.display_server = HUDElements.display_servers::WAYLAND;
         init_wayland_data();
      }
   }

   return ret;
}

EXPORT_C_(struct wl_display*) wl_display_connect_to_fd(int fd)
{
   struct wl_display *ret = nullptr;

   if (!wl_handle) {
      wl_handle = real_dlopen("libwayland-client.so", RTLD_LAZY);
   }

   if (wl_handle) {
      wl_display_connect_to_fd_ptr = (pwl_display_connect_to_fd)real_dlsym(wl_handle, "wl_display_connect_to_fd");
      wl_display_connect_ptr = (pwl_display_connect)real_dlsym(wl_handle, "wl_display_connect");

      ret = wl_display_connect_to_fd_ptr(fd);

      if (!wl_display_ptr) {
         wl_display_ptr = ret;
         HUDElements.display_server = HUDElements.display_servers::WAYLAND;
         init_wayland_data();
      }
   }

   return ret;
}
