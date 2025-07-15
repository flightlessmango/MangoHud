#include <cstdint>
#include <cstring>
#include <set>
#include <unistd.h>
#include <map>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <dlfcn.h>

#include "wayland_hook.h"
#include "real_dlsym.h"
#include "keybinds.h"

void* wl_handle = NULL;
struct xkb_context *context_xkb = NULL;

struct wayland_display
{
    int ref;
    struct wl_event_queue *queue;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct xkb_keymap *keymap_xkb;
    struct xkb_state *state_xkb;
    std::set<void *> vk_surfaces;
    std::set<KeySym> wl_pressed_keys;

    wayland_display()
    {
        ref = 1;
        queue = NULL;
        keyboard = NULL;
        keymap_xkb = NULL;
        state_xkb = NULL;
        seat = NULL;
    }

    ~wayland_display()
    {
        wl_pressed_keys.clear();
        vk_surfaces.clear();
        wl_seat_destroy(this->seat);
        wl_keyboard_destroy(this->keyboard);
        wl_event_queue_destroy(this->queue);
        if (this->keymap_xkb)
            xkb_keymap_unref(this->keymap_xkb);
        if (this->state_xkb)
            xkb_state_unref(this->state_xkb);
    }
};

std::map<struct wl_display *, wayland_display> displays;

// fixes wl_array_for_each with C++
#ifdef wl_array_for_each
#undef wl_array_for_each
#define wl_array_for_each(pos, array)					\
for (pos = (decltype(pos)) (array)->data;				\
    (array)->size != 0 &&					\
    (const char *) pos < ((const char *) (array)->data + (array)->size); \
    (pos)++)
#endif

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps);
static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {}

struct wl_seat_listener seat_listener {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void registry_handle_global(void *data, struct wl_registry* registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!data) return;
    struct wayland_display *wayland = (wayland_display *)data;
    if (strcmp(interface, wl_seat_interface.name) == 0 && !wayland->seat)
    {
        wayland->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(wayland->seat, &seat_listener, data);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name){}

static void wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size)
{
    wayland_display *wayland = (wayland_display *)data;
    char* map_shm = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (!context_xkb)
        context_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (wayland->keymap_xkb && wayland->state_xkb)
    {
        xkb_keymap_unref(wayland->keymap_xkb);
        xkb_state_unref(wayland->state_xkb);
        wayland->keymap_xkb = NULL;
        wayland->state_xkb = NULL;
    }

    wayland->keymap_xkb = xkb_keymap_new_from_string(
            context_xkb, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);

    wayland->state_xkb = xkb_state_new(wayland->keymap_xkb);

    munmap((void*)map_shm, size);
    close(fd);
}

static void wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    if (!data) return;

    wayland_display *wayland = (wayland_display *)data;

    if (!wayland->state_xkb) return;

    uint32_t *key;
    wl_array_for_each(key, keys)
    {
        xkb_keycode_t keycode = *key + 8;
        xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);
        wayland->wl_pressed_keys.insert(keysym);
    }
}

static void wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface)
{
    wayland_display *wayland = (wayland_display *)data;
    wayland->wl_pressed_keys.clear();
}

static void wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    if (!data) return;

    wayland_display *wayland = (wayland_display *)data;

    if (!wayland->state_xkb) return;

    xkb_keycode_t keycode = key + 8;
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(wayland->state_xkb, keycode);

    if (state) wayland->wl_pressed_keys.insert(keysym);
    else wayland->wl_pressed_keys.erase(keysym);
}

static void wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                                  uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    if (!data) return;

    wayland_display *wayland = (wayland_display *)data;

    if (!wayland->state_xkb) return;

    xkb_state_update_mask(wayland->state_xkb, depressed, latched, locked, 0, 0, group);
}

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

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    if (!data) return;
    wayland_display *wayland = (wayland_display *)data;

    if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
    {
        if (!wayland->keyboard)
        {
            wayland->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(wayland->keyboard, &keyboard_listener, data);
        }
    }
}

void update_wl_queue()
{
    for (const auto& display : displays)
        wl_display_dispatch_queue_pending(display.first, display.second.queue);
}

// Track vk_surface for reference counting
void init_wayland_data(struct wl_display *display, void *vk_surface)
{
    if (!display)
        return;

    if (!wl_handle)
        wl_handle = real_dlopen("libwayland-client.so.0", RTLD_LAZY);

    // failed to load
    if (!wl_handle)
        return;

    // if we already have the display then just increase its reference count
    // and add its vk_surface
    if (has_wayland_display(display))
    {
        displays[display].ref++;
        if (vk_surface)
            displays[display].vk_surfaces.insert(vk_surface);
        return;
    }

    // create a new element
    displays[display].ref = 1;
    displays[display].queue = wl_display_create_queue(display);
    if (vk_surface)
        displays[display].vk_surfaces.insert(vk_surface);
    struct wl_display *display_wrapped = (struct wl_display*)wl_proxy_create_wrapper(display);
    wl_proxy_set_queue((struct wl_proxy*)display_wrapped, displays[display].queue);
    struct wl_registry *registry = wl_display_get_registry(display_wrapped);
    wl_proxy_wrapper_destroy(display_wrapped);
    wl_registry_add_listener(registry, &registry_listener, &displays[display]);
    wl_display_roundtrip_queue(display, displays[display].queue);
    wl_display_roundtrip_queue(display, displays[display].queue);
    wl_registry_destroy(registry);
}

void wayland_data_unref(struct wl_display *display, void *vk_surface)
{
    if (has_wayland_display(display))
    {
        displays[display].ref--;
    }
    // use the VkSurface to remove reference count
    else if (vk_surface)
    {
        for (auto& display : displays)
        {
            if (display.second.vk_surfaces.count(vk_surface))
                display.second.ref--;
        }
    }

    // clear out all displays with 0 ref count
    for (auto it = displays.begin(); it != displays.end(); it++)
    {
        if (it->second.ref == 0)
            it = displays.erase(it);

        if (it == displays.end())
            break;
    }
}

bool has_wayland_display(struct wl_display *display)
{
    return displays.find(display) != displays.end();
}

bool wayland_has_keys_pressed(const std::vector<KeySym>& keys)
{
    std::map<struct wl_display *, size_t> counter;
    for (const auto& display : displays)
    {
        for (const KeySym& k : keys)
            if (display.second.wl_pressed_keys.count(k))
                counter[display.first]++;
    }

    /* if any of the displays has count == keys.size
        then it has all required keys pressed */
    for (const auto& count : counter)
    {
        if (count.second == keys.size()) return true;
    }

    return false;
}
