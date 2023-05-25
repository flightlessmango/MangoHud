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

    auto libx11 = get_libx11();

    if (!libx11->IsLoaded()) {
        SPDLOG_ERROR("X11 loader failed to load");
        failed = true;
        return false;
    }

    const char *displayid = getenv("DISPLAY");
    if (displayid) {
        display = { libx11->XOpenDisplay(displayid),
            [libx11](Display* dpy) {
                if (dpy)
                    libx11->XCloseDisplay(dpy);
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
