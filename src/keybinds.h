#pragma once
#ifdef HAVE_X11
#include "shared_x11.h"
#include "loaders/loader_x11.h"
#endif

#ifndef KeySym
typedef unsigned long KeySym;
#endif

double elapsedF2, elapsedF12, elapsedReloadCfg;
uint64_t last_f2_press, last_f12_press, reload_cfg_press;

#ifdef HAVE_X11
bool keys_are_pressed(const std::vector<KeySym>& keys) {

    if (!init_x11())
        return false;

    char keys_return[32];
    size_t pressed = 0;

    g_x11->XQueryKeymap(get_xdisplay(), keys_return);

    for (KeySym ks : keys) {
        KeyCode kc2 = g_x11->XKeysymToKeycode(get_xdisplay(), ks);

        bool isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));

        if (isPressed)
            pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

    return false;
}
#endif

#ifdef _WIN32
#include <windows.h>
bool keys_are_pressed(const std::vector<KeySym>& keys) {
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