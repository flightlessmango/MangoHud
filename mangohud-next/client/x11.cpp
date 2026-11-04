#include "x11.h"
#include "ipc_client.h"

#include <utility>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <spdlog/spdlog.h>

X11::X11(std::shared_ptr<IPCClient> ipc_) : ipc(std::move(ipc_))
{
    wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_fd < 0)
        SPDLOG_ERROR("x11: eventfd failed: {}", strerror(errno));
}

X11::~X11()
{
    {
        std::lock_guard lock(m);
        quit = true;
    }
    wake();
    if (thread.joinable())
        thread.join();
    if (wake_fd >= 0)
        close(wake_fd);
}

void X11::set_window(xcb_window_t next_window, const char* next_display)
{
    if (!next_window || wake_fd < 0)
        return;
    if (!next_display)
        next_display = std::getenv("DISPLAY");
    if (!next_display)
        return;

    {
        std::lock_guard lock(m);
        if (window == next_window && display_name == next_display)
            return;

        display_name = next_display;
        window = next_window;
        if (!thread.joinable())
            thread = std::thread([this] { run(); });
    }
    wake();
}

void X11::wake()
{
    if (wake_fd < 0)
        return;

    const uint64_t one = 1;
    ssize_t r;
    do {
        r = write(wake_fd, &one, sizeof(one));
    } while (r < 0 && errno == EINTR);
}

xcb_connection_t* X11::connect_window(xcb_window_t window, const std::string& display_name)
{
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(
        xcb_connect(display_name.c_str(), nullptr), xcb_disconnect);
    if (!connection || xcb_connection_has_error(connection.get())) {
        SPDLOG_ERROR("x11: failed to connect display={}", display_name);
        return nullptr;
    }

    const uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW |
                          XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    auto cookie = xcb_change_window_attributes_checked(connection.get(), window, XCB_CW_EVENT_MASK, &mask);
    auto* error = xcb_request_check(connection.get(), cookie);
    if (error) {
        SPDLOG_ERROR("x11: failed to watch window={} error={}", window, error->error_code);
        std::free(error);
        return nullptr;
    }
    xcb_flush(connection.get());
    watched_window = window;
    window_ancestors.clear();
    update_ancestors(connection.get());
    update_focus(connection.get());
    SPDLOG_DEBUG("x11: watching display={} window={}", display_name, window);
    return connection.release();
}

void X11::run()
{
    pthread_setname_np(pthread_self(), "mangohud_x11");
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(nullptr, xcb_disconnect);
    xcb_window_t current_window = XCB_WINDOW_NONE;
    std::string current_display;

    for (;;) {
        bool changed;
        {
            std::lock_guard lock(m);
            if (quit)
                break;
            changed = window != current_window || display_name != current_display;
            if (changed) {
                current_window = window;
                current_display = display_name;
            }
        }

        if (changed) {
            ipc->x11_focused.store(false);
            connection.reset();
            connection.reset(connect_window(current_window, current_display));
        }

        if (connection) {
            dispatch_events(connection.get());
            if (xcb_connection_has_error(connection.get())) {
                connection.reset();
                ipc->x11_focused.store(false);
            }
        }

        pollfd fds[] = {
            {wake_fd, POLLIN, 0},
            {connection ? xcb_get_file_descriptor(connection.get()) : -1, POLLIN, 0},
        };
        int r;
        do {
            r = poll(fds, 2, -1);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
            SPDLOG_ERROR("x11: poll failed: {}", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN) {
            uint64_t value;
            ssize_t n;
            do {
                n = read(wake_fd, &value, sizeof(value));
            } while (n < 0 && errno == EINTR);
        }
        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            connection.reset();
            ipc->x11_focused.store(false);
        }
    }

    ipc->x11_focused.store(false);
}

void X11::update_ancestors(xcb_connection_t* connection)
{
    std::vector<xcb_window_t> windows;
    auto window = watched_window;
    for (size_t depth = 0; window && depth < 64; ++depth) {
        windows.push_back(window);
        if (window != watched_window) {
            const uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
            xcb_change_window_attributes(connection, window, XCB_CW_EVENT_MASK, &mask);
        }
        auto* tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr);
        const auto parent = tree && tree->parent != tree->root ? tree->parent : xcb_window_t{XCB_WINDOW_NONE};
        std::free(tree);
        if (parent == window)
            break;
        window = parent;
    }
    for (auto previous : window_ancestors) {
        if (previous != watched_window && std::find(windows.begin(), windows.end(), previous) == windows.end()) {
            const uint32_t mask = XCB_EVENT_MASK_NO_EVENT;
            xcb_change_window_attributes(connection, previous, XCB_CW_EVENT_MASK, &mask);
        }
    }
    window_ancestors = std::move(windows);
    xcb_flush(connection);
}

void X11::update_focus(xcb_connection_t* connection)
{
    auto* reply = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);
    if (!reply)
        return;
    const auto input_focus = reply->focus;
    std::free(reply);

    bool focused = false;
    auto window = input_focus;
    for (size_t depth = 0; window > XCB_INPUT_FOCUS_POINTER_ROOT && depth < 64; ++depth) {
        if (std::find(window_ancestors.begin(), window_ancestors.end(), window) != window_ancestors.end()) {
            focused = true;
            break;
        }
        auto* tree = xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr);
        const auto parent = tree && tree->parent != tree->root ? tree->parent : xcb_window_t{XCB_WINDOW_NONE};
        std::free(tree);
        if (parent == window)
            break;
        window = parent;
    }
    SPDLOG_DEBUG("x11: focus window={} render_window={} focused={}", input_focus, watched_window, focused);
    ipc->x11_focused.store(focused);
}

void X11::dispatch_events(xcb_connection_t* connection)
{
    while (auto* event = xcb_poll_for_event(connection)) {
        switch (event->response_type & 0x7f) {
            case XCB_REPARENT_NOTIFY:
                update_ancestors(connection);
                update_focus(connection);
                break;
            case XCB_FOCUS_IN:
            case XCB_FOCUS_OUT: {
                auto* focus = reinterpret_cast<xcb_focus_in_event_t*>(event);
                // Grabs are temporary routing changes, not application focus changes.
                if (focus->mode == XCB_NOTIFY_MODE_NORMAL || focus->mode == XCB_NOTIFY_MODE_WHILE_GRABBED)
                    update_focus(connection);
                SPDLOG_DEBUG("x11: keyboard entered={} window={} mode={} detail={}",
                             (event->response_type & 0x7f) == XCB_FOCUS_IN,
                             focus->event, focus->mode, focus->detail);
                break;
            }
            case XCB_ENTER_NOTIFY:
            case XCB_LEAVE_NOTIFY: {
                auto* crossing = reinterpret_cast<xcb_enter_notify_event_t*>(event);
                SPDLOG_DEBUG("x11: pointer entered={} window={} mode={} detail={}",
                             (event->response_type & 0x7f) == XCB_ENTER_NOTIFY,
                             crossing->event, crossing->mode, crossing->detail);
                break;
            }
        }
        std::free(event);
    }
}
