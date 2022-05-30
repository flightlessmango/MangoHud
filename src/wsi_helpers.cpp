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

bool window_has_focus(const wsi_connection* conn)
{
    if (!conn)
        return true;

#ifdef VK_USE_PLATFORM_XCB_KHR
    if (conn->xcb.conn)
        return check_window_focus(conn->xcb.conn, conn->xcb.window);
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
    if (conn->xlib.dpy)
        return check_window_focus(conn->xlib.dpy, conn->xlib.window);
#endif
    return true;
}
