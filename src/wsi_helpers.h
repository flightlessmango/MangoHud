#pragma once
#include <functional>
#ifdef VK_USE_PLATFORM_XLIB_KHR
#include "loaders/loader_x11.h"
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xproto.h>
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <wayland-client.h>
#endif


struct wsi_connection
{
    std::function<void(bool)> focus_changed;

#ifdef VK_USE_PLATFORM_XCB_KHR
    struct xcb {
        xcb_connection_t *conn = nullptr;
        xcb_window_t window = 0;
    } xcb;
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    struct xlib {
        Display *dpy = nullptr;
        Window window = 0;
        int evmask = 0;
    } xlib;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    struct wl {
        wl_display *display;
        wl_surface *surface;
        bool has_focus;
    } wl;
#endif
};

// struct wsi_connection;
// bool check_window_focus(const wsi_connection&);
