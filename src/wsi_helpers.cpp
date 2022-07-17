#include <unistd.h>
#include <sys/mman.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <map>
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#include <xkbcommon/xkbcommon.h>
#endif
#include "wsi_helpers.h"
#include "keybinds.h" //TODO global funcs or pass by pointer with wsi_connection?

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


// -------- Wayland -------- //

struct table_entry {
//     xkb_keysym_t symbol;
    xkb_keycode_t code;
    size_t level;
    bool operator==(const table_entry& r) const
    {
        return code == r.code && level == r.level;
    }
};

namespace std {

    template <>
    struct hash<table_entry>
    {
        std::size_t operator()(const table_entry& k) const
        {
            using std::hash;
            return (hash<xkb_keycode_t>()(k.code) ^ (hash<size_t>()(k.level) << 1));
        }
    };

}

static std::unordered_map<xkb_keysym_t, std::vector<table_entry>> g_keysym_map;

struct wl_state
{
//    wl_shell *shell;
//    wl_seat *seat;
    xkb_context *ctx;
    xkb_keymap *keymap;
    struct xkb_state *state;
};

// #ifdef VK_USE_PLATFORM_WAYLAND_KHR
static std::unordered_map<xkb_keycode_t, bool> xkb_state;
void wl_key_pressed(const xkb_keycode_t key, uint32_t state)
{
    xkb_state[key] = !!state;
}

bool wl_keys_are_pressed(const std::vector<xkb_keysym_t>& keys) {
    size_t pressed = 0;
    for (auto ks : keys) {
        auto kcs = g_keysym_map[ks];
//         SPDLOG_DEBUG("code: {}, sym: {:08X}", kc.code, ks);
        for (const auto& entry : kcs)
            if (xkb_state[entry.code])
                pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

   return false;
}
// #endif

static void key_iter(struct xkb_keymap* map, xkb_keycode_t code, void* data)
{
    size_t n_levels = xkb_keymap_num_levels_for_key(map, code, 0);
    for (size_t level = 0; level < n_levels; level++) {
        const xkb_keysym_t* symbols;
        size_t n = xkb_keymap_key_get_syms_by_level(map, code, 0, level, &symbols);
        for (size_t i = 0; i < n; i++)
            g_keysym_map[symbols[i]].push_back ({ code, level });
    }
}

static void keyboard_keymap (void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    SPDLOG_DEBUG("{}", __func__);
    auto conn = reinterpret_cast<wsi_connection*>(data);
    wl_state* state = reinterpret_cast<wl_state *>(conn->userdata);

    if (!state->ctx)
        return;

    assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
    char *map_shm = reinterpret_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    assert(map_shm != MAP_FAILED);

    xkb_keymap_unref(state->keymap);
    state->keymap = xkb_keymap_new_from_string(state->ctx, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_shm, size);
    close(fd);

    xkb_state_unref(state->state);
    state->state = xkb_state_new(state->keymap);
    xkb_keymap_key_for_each(state->keymap, key_iter, data);
}

static void keyboard_enter (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    auto conn = reinterpret_cast<wsi_connection*>(data);
    SPDLOG_DEBUG("{}: {} == {}", __func__, (void*)conn->wl.surface, (void*)surface);
    if (conn->focus_changed && surface == conn->wl.surface)
        conn->focus_changed(true);
}

static void keyboard_leave (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    SPDLOG_DEBUG("{}: {}", __func__, (void*)surface);
    auto conn = reinterpret_cast<wsi_connection*>(data);
    if (conn->focus_changed && surface == conn->wl.surface)
        conn->focus_changed(false);
}

static void keyboard_key (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    auto conn = reinterpret_cast<wsi_connection*>(data);
    wl_state* wlstate = reinterpret_cast<wl_state *>(conn->userdata);
    if (!wlstate->ctx)
        return;

    uint32_t keycode = key + 8;

//     xkb_state_update_key(wlstate->state, keycode, state ? xkb_key_direction::XKB_KEY_DOWN : xkb_key_direction::XKB_KEY_UP);
    auto level = xkb_state_key_get_level(wlstate->state, keycode, 0);
    xkb_keysym_t sym = xkb_state_key_get_one_sym(wlstate->state, keycode);
//    xkb_keysym_t sym = wlstate->xkb_keycode_map[{keycode, level}];

//     if (conn->key_pressed)
//         conn->key_pressed(keycode, state);
    wl_key_pressed(keycode, state);
    SPDLOG_DEBUG("{}: key pressed: level:{}, keycode: {}, {}, keysym: {:08X}", __func__, level, keycode, state ? "down" : "up  ", sym);
}

static void keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    SPDLOG_DEBUG("{}", __func__);
    auto conn = reinterpret_cast<wsi_connection*>(data);
    wl_state* wlstate = reinterpret_cast<wl_state *>(conn->userdata);
    if (!wlstate->ctx)
        return;

    xkb_state_update_mask(wlstate->state, mods_depressed, mods_latched,
            mods_locked, 0, 0, group);
}

static struct wl_keyboard_listener keyboard_listener = {&keyboard_keymap, &keyboard_enter, &keyboard_leave, &keyboard_key, &keyboard_modifiers};

static void seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities) {
    SPDLOG_DEBUG("{}", __func__);
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        SPDLOG_DEBUG("Got usable seat");
        struct wl_keyboard *keyboard = wl_seat_get_keyboard (seat);
        wl_keyboard_add_listener (keyboard, &keyboard_listener, data);
    }
}

static struct wl_seat_listener seat_listener = {&seat_capabilities};

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    SPDLOG_DEBUG("{}", __func__);
    if (!strcmp(interface, "wl_seat")) {
        //input_reader* ir = wl_registry_get_user_data(registry);
        auto seat = (wl_seat*)wl_registry_bind (registry, name, &wl_seat_interface, 1);
//         SPDLOG_DEBUG("wl_seat_add_listener {}", wl_proxy_get_listener((wl_proxy*)seat));
        wl_seat_add_listener (seat, &seat_listener, data);
    }
}

static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name) {

}

static struct wl_registry_listener registry_listener = {&registry_add_object, &registry_remove_object};

void wsi_wayland_init(wsi_connection& conn)
{
    auto registry = wl_display_get_registry(conn.wl.display);
    auto state = new wl_state();
    state->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!state->ctx)
        SPDLOG_ERROR("Failed to create XKB context!");
    conn.userdata = state;
    wl_registry_add_listener(registry, &registry_listener, &conn);
    wl_display_roundtrip(conn.wl.display);
}

void wsi_wayland_deinit(wsi_connection& conn)
{
    wl_state* state = reinterpret_cast<wl_state *>(conn.userdata);
    xkb_state_unref(state->state);
    xkb_keymap_unref(state->keymap);
    xkb_context_unref(state->ctx);
    delete reinterpret_cast<wl_state *>(conn.userdata);
}
