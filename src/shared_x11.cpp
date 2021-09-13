#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <spdlog/spdlog.h>
#include "shared_x11.h"
#include "loaders/loader_x11.h"

static std::unique_ptr<Display, std::function<void(Display*)>> display;

bool init_x11() {
    static bool failed = false;
    if (failed)
        return false;

    if (display)
        return true;

    if (!g_x11->IsLoaded()) {
        SPDLOG_ERROR("X11 loader failed to load");
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
        SPDLOG_ERROR("XOpenDisplay failed to open display '{}'", displayid);

    return !!display;
}

Display* get_xdisplay()
{
    return display.get();
}
