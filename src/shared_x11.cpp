#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include "shared_x11.h"
#include "loaders/loader_x11.h"

using namespace std::chrono_literals;

static std::unique_ptr<Display, std::function<void(Display*)>> display;
char keys_return[32]{};

class x11_poller
{
    std::thread thread;
    bool quit;

    void poll()
    {
        while (!quit)
        {
            libx11->XQueryKeymap(get_xdisplay(), keys_return);
            std::this_thread::sleep_for(10ms);
        }
    }

public:
    std::shared_ptr<libx11_loader> libx11;
    x11_poller(std::shared_ptr<libx11_loader> x) : libx11(x) {}

    void start()
    {
        stop();
        quit = false;
        thread = std::thread(&x11_poller::poll, this);
    }

    void stop()
    {
        quit = true;
        if (thread.joinable())
            thread.join();
    }
};

bool init_x11() {
    std::shared_ptr<x11_poller> poller;
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
        poller = std::make_shared<x11_poller>( g_x11 );
        display = { g_x11->XOpenDisplay(displayid),
            [poller](Display* dpy) {
                poller->stop();
                if (dpy)
                    poller->libx11->XCloseDisplay(dpy);
            }
        };

        if (display)
            poller->start();
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
