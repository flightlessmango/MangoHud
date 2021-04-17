#include <thread>
#include <iostream>
#include <functional>
#include <memory>
#include <chrono>
#include <vector>
#include <map>
#include <mutex>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libudev.h>
#include <libinput.h>
#include <cstring>
#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "keybinds_libinput.h"
#include "real_dlsym.h"
#include <dlfcn.h>

struct key_state {
    uint32_t key;
    bool pressed;
    int modifiers;
};

//F2 = 60
//F4 = 62
//F12 = 88
static std::mutex input_mutex;

static int open_restricted(const char *path, int flags, void *user_data)
{
#ifndef NDEBUG
    std::cerr << __func__ << ": " << path << "\n";
#endif
    int fd = open(path, flags);
    if (fd < 0)
    {
        perror("MANGOHUD: libinput");
        return -errno;
    }
    return fd;
}

    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shell *shell = nullptr;
    wl_seat *seat = nullptr;

static void close_restricted(int fd, void *user_data)
{
    close(fd);
}

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name);
static struct wl_registry_listener registry_listener = {&registry_add_object, &registry_remove_object};
void *g_wl_display = nullptr;

struct input_reader {
    bool quit = false;
    std::thread thread;
    bool thread_started = false;
    std::vector<std::vector<key_state>> key_states;

    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shell *shell = nullptr;
    wl_seat *seat = nullptr;

    input_reader(const std::vector<std::vector<key_state>>& key_states_)
    : key_states(key_states_)
    {
        std::cerr << __func__ << "  " << g_wl_display << "\n";
        if (!g_wl_display) return;
        display = (wl_display*) g_wl_display;
        thread_started = true;
        //display = wl_display_connect (nullptr);
        struct wl_registry *registry = wl_display_get_registry (display);
        wl_registry_add_listener (registry, &registry_listener, nullptr);
        wl_display_roundtrip (display);
        //thread = std::thread(&input_reader::event_thread, this);
    }

    ~input_reader()
    {
        quit = true;
        if (thread.joinable())
            thread.join();
    }

    void event_thread()
    {
        //while (!quit) {
        //    wl_display_dispatch_pending (display);
        //}
        return;
        static struct libinput_interface interface;
        interface.open_restricted = open_restricted;
        interface.close_restricted = close_restricted;

        struct libinput *li;
        struct libinput_event *event;
        struct udev *udev = udev_new();

        const char* seat = getenv("XDG_SEAT");
        if (!seat)
            seat = "seat0";

        li = libinput_udev_create_context(&interface, NULL, udev);
        libinput_udev_assign_seat(li, seat);
        libinput_dispatch(li);

        auto local_keystates = key_states;
        while (!quit) {
            bool changed = false;
            while ((event = libinput_get_event(li)) != nullptr) {

                auto event_type = libinput_event_get_type(event);

                /*if (LIBINPUT_EVENT_DEVICE_ADDED == event_type) {
                    struct libinput_device *dev = libinput_event_get_device(event);
                    const char *name = libinput_device_get_name(dev);
                    std::cerr << __func__ << ": new device: " << name << "\n";
                }*/

                if (LIBINPUT_EVENT_KEYBOARD_KEY == event_type) {
                    auto kb_ev = libinput_event_get_keyboard_event(event);
                    uint32_t key = libinput_event_keyboard_get_key(kb_ev);
                    auto kb_key_state = libinput_event_keyboard_get_key_state(kb_ev);
                    for (auto& ks : local_keystates)
                    {
                        for (auto& k : ks) {
                            if (k.key == key)
                            {
                                changed = true;
                                k.pressed = (kb_key_state == LIBINPUT_KEY_STATE_PRESSED);
                            }
                        }
                    }
                }

                libinput_event_destroy(event);
                libinput_dispatch(li);
            }

            if (changed)
            {
                std::lock_guard<std::mutex> lk(input_mutex);
                key_states = local_keystates;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            libinput_dispatch(li); //?
        }

        libinput_unref(li);
        udev_unref(udev);
        thread_started = false;
    }
};

static std::unique_ptr<input_reader> libinput_thread;


static void keyboard_keymap (void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    std::cerr << __func__ << "\n";
}

static void keyboard_enter (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    std::cerr << __func__ << "\n";
}

static void keyboard_leave (void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    std::cerr << __func__ << "\n";
}

static void keyboard_key (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    std::cerr << "Key pressed " << key <<"\n";
    std::lock_guard<std::mutex> lk(input_mutex);
    if (libinput_thread){
        for (auto& ks : libinput_thread->key_states)
        {
            for (auto& k : ks) {
                if (k.key == key)
                {
                    k.pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
                }
            }
        }
    }
}

static void keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    std::cerr << __func__ << "\n";
}

static struct wl_keyboard_listener keyboard_listener = {&keyboard_keymap, &keyboard_enter, &keyboard_leave, &keyboard_key, &keyboard_modifiers};

static void seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities) {
    std::cerr << __func__ << "\n";
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        std::cerr << "SEAT\n";
        struct wl_keyboard *keyboard = wl_seat_get_keyboard (seat);
        wl_keyboard_add_listener (keyboard, &keyboard_listener, NULL);
    }
}
static struct wl_seat_listener seat_listener = {&seat_capabilities};

static void registry_add_object (void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    std::cerr << __func__ << ": interface: " << interface << "\n";
    if (!strcmp(interface,"wl_seat")) {
        //input_reader* ir = wl_registry_get_user_data(registry);
        seat = (wl_seat*)wl_registry_bind (registry, name, &wl_seat_interface, 1);

        std::cerr << "\t wl_seat_add_listener " <<  wl_proxy_get_listener((wl_proxy*)seat)  <<"\n";
        wl_seat_add_listener (seat, &seat_listener, NULL);
    }
}
static void registry_remove_object (void *data, struct wl_registry *registry, uint32_t name) {

}

const std::map<uint16_t, uint8_t> input_map_x11_to_linux = {
    {0x20, 0x39}, /* x11:32 (XK_space) -> linux:57 (KEY_SPACE) */
    {0x21, 0x2}, /* x11:33 (XK_exclam) -> linux:2 (KEY_1) */
    {0x22, 0x28}, /* x11:34 (XK_quotedbl) -> linux:40 (KEY_APOSTROPHE) */
    {0x23, 0x4}, /* x11:35 (XK_numbersign) -> linux:4 (KEY_3) */
    {0x24, 0x5}, /* x11:36 (XK_dollar) -> linux:5 (KEY_4) */
    {0x25, 0x6}, /* x11:37 (XK_percent) -> linux:6 (KEY_5) */
    {0x26, 0x8}, /* x11:38 (XK_ampersand) -> linux:8 (KEY_7) */
    {0x27, 0x28}, /* x11:39 (XK_quotedbl) -> linux:40 (KEY_APOSTROPHE) */
    {0x28, 0xa}, /* x11:40 (XK_parenleft) -> linux:10 (KEY_9) */
    {0x29, 0xb}, /* x11:41 (XK_parenright) -> linux:11 (KEY_0) */
    {0x2a, 0x9}, /* x11:42 (XK_asterisk) -> linux:9 (KEY_8) */
    {0x2b, 0xd}, /* x11:43 (XK_plus) -> linux:13 (KEY_EQUAL) */
    {0x2c, 0x33}, /* x11:44 (XK_less) -> linux:51 (KEY_COMMA) */
    {0x2d, 0xc}, /* x11:45 (XK_underscore) -> linux:12 (KEY_MINUS) */
    {0x2e, 0x34}, /* x11:46 (XK_greater) -> linux:52 (KEY_DOT) */
    {0x2f, 0x35}, /* x11:47 (XK_question) -> linux:53 (KEY_SLASH) */
    {0x30, 0xb}, /* x11:48 (XK_parenright) -> linux:11 (KEY_0) */
    {0x31, 0x2}, /* x11:49 (XK_exclam) -> linux:2 (KEY_1) */
    {0x32, 0x3}, /* x11:50 (XK_at) -> linux:3 (KEY_2) */
    {0x33, 0x4}, /* x11:51 (XK_numbersign) -> linux:4 (KEY_3) */
    {0x34, 0x5}, /* x11:52 (XK_dollar) -> linux:5 (KEY_4) */
    {0x35, 0x6}, /* x11:53 (XK_percent) -> linux:6 (KEY_5) */
    {0x36, 0x7}, /* x11:54 (XK_asciicircum) -> linux:7 (KEY_6) */
    {0x37, 0x8}, /* x11:55 (XK_ampersand) -> linux:8 (KEY_7) */
    {0x38, 0x9}, /* x11:56 (XK_asterisk) -> linux:9 (KEY_8) */
    {0x39, 0xa}, /* x11:57 (XK_parenleft) -> linux:10 (KEY_9) */
    {0x3a, 0x27}, /* x11:58 (XK_colon) -> linux:39 (KEY_SEMICOLON) */
    {0x3b, 0x27}, /* x11:59 (XK_colon) -> linux:39 (KEY_SEMICOLON) */
    {0x3c, 0x33}, /* x11:60 (XK_less) -> linux:51 (KEY_COMMA) */
    {0x3d, 0xd}, /* x11:61 (XK_plus) -> linux:13 (KEY_EQUAL) */
    {0x3e, 0x34}, /* x11:62 (XK_greater) -> linux:52 (KEY_DOT) */
    {0x3f, 0x35}, /* x11:63 (XK_question) -> linux:53 (KEY_SLASH) */
    {0x40, 0x3}, /* x11:64 (XK_at) -> linux:3 (KEY_2) */
    {0x41, 0x1e}, /* x11:65 (XK_a) -> linux:30 (KEY_A) */
    {0x42, 0x30}, /* x11:66 (XK_b) -> linux:48 (KEY_B) */
    {0x43, 0x2e}, /* x11:67 (XK_c) -> linux:46 (KEY_C) */
    {0x44, 0x20}, /* x11:68 (XK_d) -> linux:32 (KEY_D) */
    {0x45, 0x12}, /* x11:69 (XK_e) -> linux:18 (KEY_E) */
    {0x46, 0x21}, /* x11:70 (XK_f) -> linux:33 (KEY_F) */
    {0x47, 0x22}, /* x11:71 (XK_g) -> linux:34 (KEY_G) */
    {0x48, 0x23}, /* x11:72 (XK_h) -> linux:35 (KEY_H) */
    {0x49, 0x17}, /* x11:73 (XK_i) -> linux:23 (KEY_I) */
    {0x4a, 0x24}, /* x11:74 (XK_j) -> linux:36 (KEY_J) */
    {0x4b, 0x25}, /* x11:75 (XK_k) -> linux:37 (KEY_K) */
    {0x4c, 0x26}, /* x11:76 (XK_l) -> linux:38 (KEY_L) */
    {0x4d, 0x32}, /* x11:77 (XK_m) -> linux:50 (KEY_M) */
    {0x4e, 0x31}, /* x11:78 (XK_n) -> linux:49 (KEY_N) */
    {0x4f, 0x18}, /* x11:79 (XK_o) -> linux:24 (KEY_O) */
    {0x50, 0x19}, /* x11:80 (XK_p) -> linux:25 (KEY_P) */
    {0x51, 0x10}, /* x11:81 (XK_q) -> linux:16 (KEY_Q) */
    {0x52, 0x13}, /* x11:82 (XK_r) -> linux:19 (KEY_R) */
    {0x53, 0x1f}, /* x11:83 (XK_s) -> linux:31 (KEY_S) */
    {0x54, 0x14}, /* x11:84 (XK_t) -> linux:20 (KEY_T) */
    {0x55, 0x16}, /* x11:85 (XK_u) -> linux:22 (KEY_U) */
    {0x56, 0x2f}, /* x11:86 (XK_v) -> linux:47 (KEY_V) */
    {0x57, 0x11}, /* x11:87 (XK_w) -> linux:17 (KEY_W) */
    {0x58, 0x2d}, /* x11:88 (XK_x) -> linux:45 (KEY_X) */
    {0x59, 0x15}, /* x11:89 (XK_y) -> linux:21 (KEY_Y) */
    {0x5a, 0x2c}, /* x11:90 (XK_z) -> linux:44 (KEY_Z) */
    {0x5b, 0x1a}, /* x11:91 (XK_braceleft) -> linux:26 (KEY_LEFTBRACE) */
    {0x5c, 0x2b}, /* x11:92 (XK_bar) -> linux:43 (KEY_BACKSLASH) */
    {0x5d, 0x1b}, /* x11:93 (XK_braceright) -> linux:27 (KEY_RIGHTBRACE) */
    {0x5e, 0x7}, /* x11:94 (XK_asciicircum) -> linux:7 (KEY_6) */
    {0x5f, 0xc}, /* x11:95 (XK_underscore) -> linux:12 (KEY_MINUS) */
    {0x60, 0x29}, /* x11:96 (XK_asciitilde) -> linux:41 (KEY_GRAVE) */
    {0x61, 0x1e}, /* x11:97 (XK_a) -> linux:30 (KEY_A) */
    {0x62, 0x30}, /* x11:98 (XK_b) -> linux:48 (KEY_B) */
    {0x63, 0x2e}, /* x11:99 (XK_c) -> linux:46 (KEY_C) */
    {0x64, 0x20}, /* x11:100 (XK_d) -> linux:32 (KEY_D) */
    {0x65, 0x12}, /* x11:101 (XK_e) -> linux:18 (KEY_E) */
    {0x66, 0x21}, /* x11:102 (XK_f) -> linux:33 (KEY_F) */
    {0x67, 0x22}, /* x11:103 (XK_g) -> linux:34 (KEY_G) */
    {0x68, 0x23}, /* x11:104 (XK_h) -> linux:35 (KEY_H) */
    {0x69, 0x17}, /* x11:105 (XK_i) -> linux:23 (KEY_I) */
    {0x6a, 0x24}, /* x11:106 (XK_j) -> linux:36 (KEY_J) */
    {0x6b, 0x25}, /* x11:107 (XK_k) -> linux:37 (KEY_K) */
    {0x6c, 0x26}, /* x11:108 (XK_l) -> linux:38 (KEY_L) */
    {0x6d, 0x32}, /* x11:109 (XK_m) -> linux:50 (KEY_M) */
    {0x6e, 0x31}, /* x11:110 (XK_n) -> linux:49 (KEY_N) */
    {0x6f, 0x18}, /* x11:111 (XK_o) -> linux:24 (KEY_O) */
    {0x70, 0x19}, /* x11:112 (XK_p) -> linux:25 (KEY_P) */
    {0x71, 0x10}, /* x11:113 (XK_q) -> linux:16 (KEY_Q) */
    {0x72, 0x13}, /* x11:114 (XK_r) -> linux:19 (KEY_R) */
    {0x73, 0x1f}, /* x11:115 (XK_s) -> linux:31 (KEY_S) */
    {0x74, 0x14}, /* x11:116 (XK_t) -> linux:20 (KEY_T) */
    {0x75, 0x16}, /* x11:117 (XK_u) -> linux:22 (KEY_U) */
    {0x76, 0x2f}, /* x11:118 (XK_v) -> linux:47 (KEY_V) */
    {0x77, 0x11}, /* x11:119 (XK_w) -> linux:17 (KEY_W) */
    {0x78, 0x2d}, /* x11:120 (XK_x) -> linux:45 (KEY_X) */
    {0x79, 0x15}, /* x11:121 (XK_y) -> linux:21 (KEY_Y) */
    {0x7a, 0x2c}, /* x11:122 (XK_z) -> linux:44 (KEY_Z) */
    {0x7b, 0x1a}, /* x11:123 (XK_braceleft) -> linux:26 (KEY_LEFTBRACE) */
    {0x7c, 0x2b}, /* x11:124 (XK_bar) -> linux:43 (KEY_BACKSLASH) */
    {0x7d, 0x1b}, /* x11:125 (XK_braceright) -> linux:27 (KEY_RIGHTBRACE) */
    {0x7e, 0x29}, /* x11:126 (XK_asciitilde) -> linux:41 (KEY_GRAVE) */
    {0xd7, 0x37}, /* x11:215 (XK_multiply) -> linux:55 (KEY_KPASTERISK) */
    {0xff08, 0xe}, /* x11:65288 (XK_BackSpace) -> linux:14 (KEY_BACKSPACE) */
    {0xff09, 0xf}, /* x11:65289 (XK_Tab) -> linux:15 (KEY_TAB) */
    {0xff0d, 0x1c}, /* x11:65293 (XK_Return) -> linux:28 (KEY_ENTER) */
    {0xff13, 0x77}, /* x11:65299 (XK_Pause) -> linux:119 (KEY_PAUSE) */
    {0xff14, 0x46}, /* x11:65300 (XK_Scroll_Lock) -> linux:70 (KEY_SCROLLLOCK) */
    {0xff15, 0x63}, /* x11:65301 (XK_Sys_Req) -> linux:99 (KEY_SYSRQ) */
    {0xff1b, 0x1}, /* x11:65307 (XK_Escape) -> linux:1 (KEY_ESC) */
    {0xff50, 0x66}, /* x11:65360 (XK_Home) -> linux:102 (KEY_HOME) */
    {0xff51, 0x69}, /* x11:65361 (XK_Left) -> linux:105 (KEY_LEFT) */
    {0xff52, 0x67}, /* x11:65362 (XK_Up) -> linux:103 (KEY_UP) */
    {0xff53, 0x6a}, /* x11:65363 (XK_Right) -> linux:106 (KEY_RIGHT) */
    {0xff54, 0x6c}, /* x11:65364 (XK_Down) -> linux:108 (KEY_DOWN) */
    {0xff55, 0x68}, /* x11:65365 (XK_Page_Up) -> linux:104 (KEY_PAGEUP) */
    {0xff56, 0x6d}, /* x11:65366 (XK_Page_Down) -> linux:109 (KEY_PAGEDOWN) */
    {0xff57, 0x6b}, /* x11:65367 (XK_End) -> linux:107 (KEY_END) */
    {0xff60, 0x161}, /* x11:65376 (XK_Select) -> linux:353 (KEY_SELECT) */
    {0xff63, 0x6e}, /* x11:65379 (XK_Insert) -> linux:110 (KEY_INSERT) */
    {0xff6a, 0x8a}, /* x11:65386 (XK_Help) -> linux:138 (KEY_HELP) */
    {0xff7f, 0x45}, /* x11:65407 (XK_Num_Lock) -> linux:69 (KEY_NUMLOCK) */
    {0xff8d, 0x60}, /* x11:65421 (XK_KP_Enter) -> linux:96 (KEY_KPENTER) */
    {0xffab, 0x4e}, /* x11:65451 (XK_KP_Add) -> linux:78 (KEY_KPPLUS) */
    {0xffac, 0x5f}, /* x11:65452 (XK_KP_Separator) -> linux:95 (KEY_KPJPCOMMA) */
    {0xffad, 0x4a}, /* x11:65453 (XK_KP_Subtract) -> linux:74 (KEY_KPMINUS) */
    {0xffae, 0x53}, /* x11:65454 (XK_KP_Decimal) -> linux:83 (KEY_KPDOT) */
    {0xffaf, 0x62}, /* x11:65455 (XK_KP_Divide) -> linux:98 (KEY_KPSLASH) */
    {0xffb0, 0x52}, /* x11:65456 (XK_KP_0) -> linux:82 (KEY_KP0) */
    {0xffb1, 0x4f}, /* x11:65457 (XK_KP_1) -> linux:79 (KEY_KP1) */
    {0xffb2, 0x50}, /* x11:65458 (XK_KP_2) -> linux:80 (KEY_KP2) */
    {0xffb3, 0x51}, /* x11:65459 (XK_KP_3) -> linux:81 (KEY_KP3) */
    {0xffb4, 0x4b}, /* x11:65460 (XK_KP_4) -> linux:75 (KEY_KP4) */
    {0xffb5, 0x4c}, /* x11:65461 (XK_KP_5) -> linux:76 (KEY_KP5) */
    {0xffb6, 0x4d}, /* x11:65462 (XK_KP_6) -> linux:77 (KEY_KP6) */
    {0xffb7, 0x47}, /* x11:65463 (XK_KP_7) -> linux:71 (KEY_KP7) */
    {0xffb8, 0x48}, /* x11:65464 (XK_KP_8) -> linux:72 (KEY_KP8) */
    {0xffb9, 0x49}, /* x11:65465 (XK_KP_9) -> linux:73 (KEY_KP9) */
    {0xffbd, 0x75}, /* x11:65469 (XK_KP_Equal) -> linux:117 (KEY_KPEQUAL) */
    {0xffbe, 0x3b}, /* x11:65470 (XK_F1) -> linux:59 (KEY_F1) */
    {0xffbf, 0x3c}, /* x11:65471 (XK_F2) -> linux:60 (KEY_F2) */
    {0xffc0, 0x3d}, /* x11:65472 (XK_F3) -> linux:61 (KEY_F3) */
    {0xffc1, 0x3e}, /* x11:65473 (XK_F4) -> linux:62 (KEY_F4) */
    {0xffc2, 0x3f}, /* x11:65474 (XK_F5) -> linux:63 (KEY_F5) */
    {0xffc3, 0x40}, /* x11:65475 (XK_F6) -> linux:64 (KEY_F6) */
    {0xffc4, 0x41}, /* x11:65476 (XK_F7) -> linux:65 (KEY_F7) */
    {0xffc5, 0x42}, /* x11:65477 (XK_F8) -> linux:66 (KEY_F8) */
    {0xffc6, 0x43}, /* x11:65478 (XK_F9) -> linux:67 (KEY_F9) */
    {0xffc7, 0x44}, /* x11:65479 (XK_F10) -> linux:68 (KEY_F10) */
    {0xffc8, 0x57}, /* x11:65480 (XK_F11) -> linux:87 (KEY_F11) */
    {0xffc9, 0x58}, /* x11:65481 (XK_F12) -> linux:88 (KEY_F12) */
    {0xffe1, 0x2a}, /* x11:65505 (XK_Shift_L) -> linux:42 (KEY_LEFTSHIFT) */
    {0xffe2, 0x36}, /* x11:65506 (XK_Shift_R) -> linux:54 (KEY_RIGHTSHIFT) */
    {0xffe3, 0x1d}, /* x11:65507 (XK_Control_L) -> linux:29 (KEY_LEFTCTRL) */
    {0xffe4, 0x61}, /* x11:65508 (XK_Control_R) -> linux:97 (KEY_RIGHTCTRL) */
    {0xffe5, 0x3a}, /* x11:65509 (XK_Caps_Lock) -> linux:58 (KEY_CAPSLOCK) */
    {0xffe7, 0x7d}, /* x11:65511 (XK_Meta_L) -> linux:125 (KEY_LEFTMETA) */
    {0xffe8, 0x7e}, /* x11:65512 (XK_Meta_R) -> linux:126 (KEY_RIGHTMETA) */
    {0xffe9, 0x38}, /* x11:65513 (XK_Alt_L) -> linux:56 (KEY_LEFTALT) */
    {0xffea, 0x64}, /* x11:65514 (XK_Alt_R) -> linux:100 (KEY_RIGHTALT) */
    {0xffff, 0x6f}, /* x11:65535 (XK_Delete) -> linux:111 (KEY_DELETE) */
};

std::vector<key_state> get_linux_key(const std::vector<KeySym>& x11_keys)
{
    std::vector<key_state> keys;
    for (const auto x11_key : x11_keys)
    {
        auto it = input_map_x11_to_linux.find(x11_key);
        if (it != input_map_x11_to_linux.end())
            keys.push_back({it->second, false, 0});
        else
        {
            std::cerr << "MANGOHUD: cannot remap X11 key to libinput: " << x11_key << "\n";
            break;
        }
    }

    if (keys.size() == x11_keys.size())
        return keys;
    return {};
}

void start_input(const overlay_params& params)
{
    if (libinput_thread && libinput_thread->thread_started)
        return;

    std::lock_guard<std::mutex> lk(input_mutex);
    std::vector<std::vector<key_state>> key_states;
    key_states.push_back(get_linux_key(params.toggle_hud));
    key_states.push_back(get_linux_key(params.toggle_logging));
    key_states.push_back(get_linux_key(params.reload_cfg));
    key_states.push_back(get_linux_key(params.upload_log));
    key_states.push_back(get_linux_key(params.upload_logs));
    key_states.push_back(get_linux_key(params.toggle_fps_limit));

    libinput_thread = std::make_unique<input_reader>(key_states);
}

bool libinput_key_is_pressed(std::vector<KeySym> x11_keybinds)
{
    if (!libinput_thread)
        return false;

    std::lock_guard<std::mutex> lk(input_mutex);
    auto keybinds = get_linux_key(x11_keybinds);
    if (keybinds.empty())
        return false;

    for (const auto& ks : libinput_thread->key_states)
    {
        if (ks.size() != keybinds.size())
            continue;

        size_t pressed = 0;
        for (const auto k : ks)
        {
            for (const auto keybind : keybinds)
                if (k.key == keybind.key && k.pressed)
                    ++pressed;
        }

        if (pressed > 0 && pressed == x11_keybinds.size())
            return true;
    }
    return false;
}
