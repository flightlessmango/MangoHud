#pragma once
#ifndef MANGOHUD_KEYBINDS_H
#define MANGOHUD_KEYBINDS_H

#ifdef HAVE_X11
#include "shared_x11.h"
#include "loaders/loader_x11.h"
#endif
#ifdef HAVE_WAYLAND
#include "wayland_hook.h"
#endif

#ifndef KeySym
typedef unsigned long KeySym;
#endif

#if defined(HAVE_X11) || defined(HAVE_WAYLAND)
static inline bool keys_are_pressed(const std::vector<KeySym>& keys)
{
    if (keys.size() == 0)
        return false;

    #if defined(HAVE_WAYLAND)
    if (wl_handle)
    {
        update_wl_queue();

        if (wayland_has_keys_pressed(keys))
           return true;
    }
    #endif

    #if defined(HAVE_X11)
    if (init_x11())
    {
        char keys_return[32];
        size_t pressed = 0;

        auto libx11 = get_libx11();
        libx11->XQueryKeymap(get_xdisplay(), keys_return);

        for (KeySym ks : keys) {
            KeyCode kc2 = libx11->XKeysymToKeycode(get_xdisplay(), ks);

            bool isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));

            if (isPressed)
                pressed++;
        }

        if (pressed == keys.size()) {
            return true;
        }
    }
    #endif

    return false;
}
#elif defined(_WIN32)
#include <windows.h>
static inline bool keys_are_pressed(const std::vector<KeySym>& keys) {
    size_t pressed = 0;

    for (KeySym ks : keys) {
        if (GetAsyncKeyState(ks) & 0x8000)
            pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

    return false;
}
#endif

#endif //MANGOHUD_KEYBINDS_H
