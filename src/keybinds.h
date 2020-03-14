#include <X11/Xlib.h>
#include <iostream>
#include "X11/keysym.h"

double elapsedF2, elapsedF12, elapsedReloadCfg;
uint64_t last_f2_press, last_f12_press, reload_cfg_press;
pthread_t f2;
char *displayid = getenv("DISPLAY");
std::unique_ptr<Display, std::function<void(Display*)>> dpy(XOpenDisplay(displayid), [](Display* d) { XCloseDisplay(d); });

bool key_is_pressed(KeySym ks) {
    char keys_return[32];
    XQueryKeymap(dpy.get(), keys_return);
    KeyCode kc2 = XKeysymToKeycode(dpy.get(), ks);
    bool isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
    return isPressed;
}
