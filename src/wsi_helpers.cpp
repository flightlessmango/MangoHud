#include <spdlog/spdlog.h>
#include <cstring>
#include "wsi_helpers.h"

#ifdef VK_USE_PLATFORM_XCB_KHR

static bool check_window_focus(xcb_connection_t * connection, xcb_window_t window)
{
   auto reply = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);
   if (reply)
   {
      SPDLOG_DEBUG("Window: {:08x} Focus WId: {:08x}", window, reply->focus);
      bool has_focus = (window == reply->focus);
      free(reply);
      return has_focus;
   }

//    xcb_query_tree_cookie_t cookie = xcb_query_tree(connection, reply->focus);
//    xcb_query_tree_reply_t *tree_reply = nullptr;
//
//    if ((tree_reply = xcb_query_tree_reply(connection, cookie, nullptr))) {
//         printf("root = 0x%08x\n", tree_reply->root);
//         printf("parent = 0x%08x\n", tree_reply->parent);
//
//         xcb_window_t *children = xcb_query_tree_children(tree_reply);
//         for (int i = 0; i < xcb_query_tree_children_length(tree_reply); i++)
//             printf("child window = 0x%08x\n", children[i]);
//
//         free(reply);
//     }

   return true;
}
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
static bool check_window_focus(Display *disp, Window window)
{
    if (!g_x11 || !g_x11->IsLoaded())
        return true;

    Window focus;
    int revert_to;

    if (!g_x11->XGetInputFocus(disp, &focus, &revert_to))
        return true;

    SPDLOG_DEBUG("Window: {:08x}, Focus: {:08x}", window, focus);

    // wine vulkan surface's window is a child of "main" window?
    Window w = window;
    Window parent = window;
    Window root = None;
    Window *children;
    unsigned int nchildren;
    Status s;

    while (parent != root) {
        w = parent;
        s = g_x11->XQueryTree(disp, w, &root, &parent, &children, &nchildren);

        if (s)
            g_x11->XFree(children);

        if (w == focus || !root)
        {
            SPDLOG_DEBUG("we got focus");
            return true;
        }

        SPDLOG_DEBUG("  get parent: window: {:08x}, parent: {:08x}, root: {:08x}", w, parent, root);
    }

    SPDLOG_DEBUG("parent: {:08x}, focus: {:08x}", w, focus);
    return false;
}
#endif

void window_has_focus(wsi_connection* conn)
{
    if (!conn || !conn->focus_changed)
        return;

#ifdef VK_USE_PLATFORM_XCB_KHR
    if (conn->xcb.conn)
        conn->focus_changed(check_window_focus(conn->xcb.conn, conn->xcb.window));
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
    if (conn->xlib.dpy)
        conn->focus_changed(check_window_focus(conn->xlib.dpy, conn->xlib.window));
#endif
}

struct interfaces_
{
   wl_shell *shell;
   wl_seat *seat;
   wsi_connection *wsi;
};

static void keyboard_keymap (void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    SPDLOG_DEBUG("{}", __func__);
}

static void keyboard_enter (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    SPDLOG_DEBUG("{}", __func__);
    auto wsi = reinterpret_cast<wsi_connection*>(data);
    if (wsi->focus_changed && surface == wsi->wl.surface)
        wsi->focus_changed(true);
}

static void keyboard_leave (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    SPDLOG_DEBUG("{}", __func__);
    auto wsi = reinterpret_cast<wsi_connection*>(data);
    if (wsi->focus_changed && surface == wsi->wl.surface)
        wsi->focus_changed(false);
}

static void keyboard_key (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    SPDLOG_DEBUG("{}: key pressed: {}", __func__, key);
}

static void keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    SPDLOG_DEBUG("{}", __func__);
}

static struct wl_keyboard_listener keyboard_listener = {&keyboard_keymap, &keyboard_enter, &keyboard_leave, &keyboard_key, &keyboard_modifiers};

static void seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities) {
    SPDLOG_DEBUG("{}", __func__);
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        SPDLOG_DEBUG("SEAT");
        struct wl_keyboard *keyboard = wl_seat_get_keyboard (seat);
        wl_keyboard_add_listener (keyboard, &keyboard_listener, data);
    }
}
static struct wl_seat_listener seat_listener = {&seat_capabilities};

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    SPDLOG_DEBUG("{}", __func__);
    if (!strcmp(interface,"wl_seat")) {
        //input_reader* ir = wl_registry_get_user_data(registry);
        auto seat = (wl_seat*)wl_registry_bind (registry, name, &wl_seat_interface, 1);
//         SPDLOG_DEBUG("wl_seat_add_listener {}", wl_proxy_get_listener((wl_proxy*)seat));
        wl_seat_add_listener (seat, &seat_listener, data);
    }
}
static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name) {

}

static struct wl_registry_listener registry_listener = {&registry_add_object, &registry_remove_object};

// interfaces_ g_interfaces {};

void wayland_init(wsi_connection& conn)
{
   auto registry = wl_display_get_registry(conn.wl.display);
   wl_registry_add_listener(registry, &registry_listener, &conn);
   wl_display_roundtrip(conn.wl.display);
}
