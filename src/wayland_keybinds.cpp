#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <unistd.h>
#include <sys/mman.h>

#include "wayland_hook.h"
#include "keybinds.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#define SEAT_MAX_VERSION 5

class SeatInfo
{
public:
    SeatInfo(uint32_t name, wl_seat *seat);
    SeatInfo(const SeatInfo &info) = delete;
    SeatInfo(const SeatInfo &&info) = delete;
    ~SeatInfo();

    bool is_global(uint32_t name) const;
    bool are_pressed(const std::vector<KeySym> &syms) const;

    void init_keyboard();
    void fini_keyboard();

    void keymap(uint32_t format, int fd, size_t size);
    void key(uint32_t key, uint32_t state);
    void modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

    void clear();

private:
    uint32_t _name;
    wl_seat *_seat;
    wl_keyboard *_keyboard;
    xkb_state *_xkb_state;
    std::vector<KeySym> _pressed_syms;
};

static wl_display *wl_display_ptr = nullptr;
static wl_event_queue *wl_queue_ptr = nullptr;
static wl_registry *wl_registry_ptr = nullptr;
static xkb_context *xkb_context = nullptr;
static std::vector<std::unique_ptr<SeatInfo>> seats {};

static void wl_keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size)
{
    static_cast<SeatInfo *>(data)->keymap(format, fd, size);
}

static void wl_keyboard_enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) { }

static void wl_keyboard_leave(void *data, wl_keyboard *, uint32_t , wl_surface *)
{
    static_cast<SeatInfo *>(data)->clear();
}

static void wl_keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state)
{
    static_cast<SeatInfo *>(data)->key(key, state);
}

static void wl_keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched,
                                  uint32_t locked, uint32_t group)
{
    static_cast<SeatInfo *>(data)->modifiers(depressed, latched, locked, group);
}

static void wl_keyboard_repeat_info(void *, wl_keyboard *, int32_t, int32_t) { }

static wl_keyboard_listener keyboard_listener {
        .keymap = wl_keyboard_keymap,
        .enter = wl_keyboard_enter,
        .leave = wl_keyboard_leave,
        .key = wl_keyboard_key,
        .modifiers = wl_keyboard_modifiers,
        .repeat_info = wl_keyboard_repeat_info
};

static void seat_handle_capabilities(void *data, wl_seat *, uint32_t caps)
{
    auto info = static_cast<SeatInfo *>(data);
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
        info->init_keyboard();
    else
        info->fini_keyboard();
}

static void seat_handle_name(void *, wl_seat *, const char *) {}

static wl_seat_listener seat_listener {
        .capabilities = seat_handle_capabilities,
        .name = seat_handle_name,
};

static void registry_handle_global(void *, wl_registry *registry, uint32_t name, const char *interface,
                                   uint32_t version)
{
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (version > SEAT_MAX_VERSION)
            version = SEAT_MAX_VERSION;

        auto seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, version));
        if (seat)
            seats.push_back(std::make_unique<SeatInfo>(name, seat));
    }
}

static void registry_handle_global_remove(void *data, wl_registry *, uint32_t name)
{
    auto it = std::find_if(
            seats.begin(),
            seats.end(),
            [&name](const std::unique_ptr<SeatInfo> &s)
            {
                return s->is_global(name);
            });

    if (it != seats.end())
        seats.erase(it);
}

static wl_registry_listener registry_listener{
        .global = registry_handle_global,
        .global_remove = registry_handle_global_remove,
};


SeatInfo::SeatInfo(uint32_t name, wl_seat *seat)
        : _name(name), _seat(seat), _keyboard(nullptr),
          _xkb_state(nullptr), _pressed_syms()
{
    wl_seat_add_listener(seat, &seat_listener, this);
}

SeatInfo::~SeatInfo()
{
    fini_keyboard();

    if (wl_seat_get_version(_seat) >= WL_SEAT_RELEASE_SINCE_VERSION)
        wl_seat_release(_seat);
    else
        wl_seat_destroy(_seat);
}

bool SeatInfo::is_global(uint32_t name) const
{
    return _name == name;
}

bool SeatInfo::are_pressed(const std::vector<KeySym> &syms) const
{
    return syms == _pressed_syms;
}

void SeatInfo::init_keyboard()
{
    if (_keyboard)
        return;

    _keyboard = wl_seat_get_keyboard(_seat);
    if (_keyboard)
        wl_keyboard_add_listener(_keyboard, &keyboard_listener, this);
}

void SeatInfo::fini_keyboard()
{
    if (!_keyboard)
        return;

    xkb_state_unref(_xkb_state);
    _xkb_state = nullptr;

    if (wl_keyboard_get_version(_keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
        wl_keyboard_release(_keyboard);
    else
        wl_keyboard_destroy(_keyboard);

    _keyboard = nullptr;
}

void SeatInfo::keymap(uint32_t format, int fd, size_t size)
{
    clear();

    xkb_state_unref(_xkb_state);
    _xkb_state = nullptr;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    void *map_shm = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_shm == MAP_FAILED)
        return;

    xkb_keymap *keymap = xkb_keymap_new_from_string(
            xkb_context,
            static_cast<char *>(map_shm),
            XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_shm, size);

    if (keymap) {
        _xkb_state = xkb_state_new(keymap);
        xkb_keymap_unref(keymap);
    }
}

void SeatInfo::key(uint32_t key, uint32_t state)
{
    if (!_xkb_state)
        return;

    xkb_keycode_t code = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(_xkb_state, code);
    if (sym == XKB_KEY_NoSymbol)
        return;

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        _pressed_syms.push_back(sym);
    } else if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        auto it = std::find(
                _pressed_syms.begin(),
                _pressed_syms.end(),
                sym);
        if (it != _pressed_syms.end())
            _pressed_syms.erase(it);
    }
}

void SeatInfo::modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    if (!_xkb_state)
        return;

    xkb_state_update_mask(_xkb_state, depressed, latched, locked, 0, 0, group);
}

void SeatInfo::clear()
{
    _pressed_syms.clear();
}

void init_wayland_data(wl_display *display)
{
    if (wl_display_ptr || !display)
        return;

    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context)
        return;

    auto display_wrapped = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    if (!display_wrapped) {
        xkb_context_unref(xkb_context);
        return;
    }

    wl_queue_ptr = wl_display_create_queue(display);
    if (!wl_queue_ptr) {
        wl_proxy_wrapper_destroy(display_wrapped);
        xkb_context_unref(xkb_context);
        return;
    }

    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(display_wrapped), wl_queue_ptr);

    wl_registry_ptr = wl_display_get_registry(display_wrapped);
    wl_proxy_wrapper_destroy(display_wrapped);

    if (!wl_registry_ptr) {
        wl_event_queue_destroy(wl_queue_ptr);
        xkb_context_unref(xkb_context);
        return;
    }

    wl_registry_add_listener(wl_registry_ptr, &registry_listener, nullptr);
    wl_display_ptr = display;
}

void fini_wayland_data()
{
    if (!wl_display_ptr)
        return;

    seats.clear();
    wl_registry_destroy(wl_registry_ptr);
    wl_event_queue_destroy(wl_queue_ptr);
    xkb_context_unref(xkb_context);

    wl_display_ptr = nullptr;
}

bool any_wayland_seat_syms_are_pressed(const std::vector<KeySym> &syms)
{
    if (!wl_display_ptr) {
        return false;
    }
    wl_display_dispatch_queue_pending(wl_display_ptr, wl_queue_ptr);
    return std::any_of(seats.begin(), seats.end(), [syms](const std::unique_ptr<SeatInfo> &s) {
        return s->are_pressed(syms);
    });
}
