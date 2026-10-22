#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <xcb/xcb.h>

class IPCClient;

class X11 {
public:
    explicit X11(std::shared_ptr<IPCClient> ipc);
    ~X11();

    void set_window(xcb_window_t window, const char* display_name = nullptr);

private:
    void run();
    void wake();
    xcb_connection_t* connect_window(xcb_window_t window, const std::string& display_name);
    void dispatch_events(xcb_connection_t* connection);

    std::shared_ptr<IPCClient> ipc;
    std::mutex m;
    std::thread thread;
    int wake_fd = -1;
    bool quit = false;
    xcb_window_t window = XCB_WINDOW_NONE;
    std::string display_name;
};
