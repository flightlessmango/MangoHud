#include "shared_x11.h"
#include "loaders/loader_x11.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>

static std::unique_ptr<Display, std::function<void(Display*)>> display;

bool init_x11() {
    static bool failed = false;
    if (failed)
        return false;

    if (display)
        return true;

    if (!g_x11->IsLoaded()) {
        std::cerr << "MANGOHUD: X11 loader failed to load\n";
        failed = true;
        return false;
    }

    const char *displayid = getenv("DISPLAY");
    if (displayid) {
        auto local_x11 = g_x11;
        display = { g_x11->XOpenDisplay(displayid),
            [local_x11](Display* dpy) {
                if (dpy)
                    local_x11->XCloseDisplay(dpy);
            }
        };
    }

    failed = !display;
    if (failed)
        std::cerr << "MANGOHUD: XOpenDisplay failed to open display '" << displayid << "'\n";

    return !!display;
}

Display* get_xdisplay()
{
    return display.get();
}
