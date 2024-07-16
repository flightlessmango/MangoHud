#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <spdlog/spdlog.h>
#include "shared_x11.h"
#include "loaders/loader_x11.h"
#include "hud_elements.h"

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
    if (failed && displayid)
        SPDLOG_ERROR("XOpenDisplay failed to open display '{}'", displayid);
    
    if (!displayid)
        SPDLOG_DEBUG("DISPLAY env is not set");

    if (display && HUDElements.display_server == HUDElements.display_servers::UNKNOWN) {
        int opcode, event, error;
        if (libx11->XQueryExtension(display.get(), "XWAYLAND", &opcode, &event, &error))
		    HUDElements.display_server = HUDElements.display_servers::XWAYLAND;
        else
            HUDElements.display_server = HUDElements.display_servers::XORG;

	}

    return !!display;
}

Display* get_xdisplay()
{
    return display.get();
}
