#pragma once
#include <functional>
#include <vector>

#ifdef HAVE_XKBCOMMON
#include <xkbcommon/xkbcommon.h>
#else
typedef uint32_t xkb_keysym_t;
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
#include "loaders/loader_x11.h"
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xproto.h>
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
bool wl_keys_are_pressed(const std::vector<xkb_keysym_t>& keys);
void wl_key_pressed(const xkb_keycode_t key, uint32_t state);
#endif

struct wsi_connection
{
    std::function<void(bool)> focus_changed;
    std::function<void(xkb_keysym_t, uint32_t)> key_pressed;
    std::function<bool(const std::vector<xkb_keysym_t>& keys)> keys_are_pressed;
    void* userdata {};

#ifdef VK_USE_PLATFORM_XCB_KHR
    struct xcb {
        xcb_connection_t *conn;
        xcb_window_t window;
    } xcb {};
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    struct xlib {
        Display *dpy;
        Window window;
        int evmask;
    } xlib {};
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    struct wl {
        wl_display *display;
        wl_surface *surface;
    } wl {};
#endif
};

// struct wsi_connection;
// bool check_window_focus(const wsi_connection&);

void wsi_wayland_init(wsi_connection& conn);
void wsi_wayland_deinit(wsi_connection& conn);


