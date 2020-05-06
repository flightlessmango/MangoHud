#pragma once
#include "shared_x11.h"
#include "loaders/loader_x11.h"

#ifndef KeySym
typedef unsigned long KeySym;
#endif

double elapsedF2, elapsedF12, elapsedReloadCfg;
uint64_t last_f2_press, last_f12_press, reload_cfg_press;

#ifdef HAVE_X11
bool keys_are_pressed(std::vector<KeySym>& keys) {

    if (!init_x11())
        return false;

    char keys_return[32];
    size_t pressed = 0;

    g_x11->XQueryKeymap(get_xdisplay(), keys_return);

    for(int i=0; i < keys.size(); i++){
        KeyCode kc2 = g_x11->XKeysymToKeycode(get_xdisplay(), keys[i]);

        bool isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));

        if(isPressed)
        {
            pressed++;
        }
    }

    if(pressed > 0 && pressed == keys.size())
    {
        return true;
    }

    return false;
}
#endif