#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
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
    void update_ancestors(xcb_connection_t* connection);
    void update_focus(xcb_connection_t* connection);
    xcb_window_t watched_window = XCB_WINDOW_NONE;
    std::vector<xcb_window_t> window_ancestors;

    std::shared_ptr<IPCClient> ipc;
    std::mutex m;
    std::thread thread;
    int wake_fd = -1;
    bool quit = false;
    xcb_window_t window = XCB_WINDOW_NONE;
    std::string display_name;
};
