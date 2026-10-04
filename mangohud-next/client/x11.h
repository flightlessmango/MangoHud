#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <xcb/xcb.h>

class IPCClient;

class X11 {
public:
    explicit X11(std::shared_ptr<IPCClient> ipc);
    ~X11();

    void set_window(xcb_window_t window, const char* display_name = nullptr);
    void dispatch_events();

private:
    std::shared_ptr<IPCClient> ipc;
    std::mutex m;
    xcb_connection_t* connection = nullptr;
    xcb_window_t window = XCB_WINDOW_NONE;
    std::string display_name;
};
