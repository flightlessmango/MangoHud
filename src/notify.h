#pragma once
#ifndef MANGOHUD_NOTIFY_H
#define MANGOHUD_NOTIFY_H

#include <thread>
#include <mutex>
#include <atomic>
#include "overlay_params.h"

struct notify_thread
{
    ~notify_thread();

    int fd = -1, wd = -1;
    overlay_params *params = nullptr;
    std::atomic_bool quit = false;
    std::mutex mutex;
    std::thread thread;
};

bool start_notifier(notify_thread& nt);

// Backend of the config file watcher, implemented per platform in
// notify_<os>.cpp. nt.fd is the event queue and nt.wd the watch, in whatever
// form the platform has them. The queue is non-blocking.
enum {
    NOTIFY_MODIFIED = 1 << 0, // contents changed
    NOTIFY_REMOVED  = 1 << 1, // file deleted or replaced, the watch has to be added again
};

bool notify_open(notify_thread& nt);
bool notify_watch(notify_thread& nt, const char* path); // replaces the current watch
unsigned notify_poll(notify_thread& nt);                // NOTIFY_* flags, 0 if nothing happened
void notify_close(notify_thread& nt);

#endif //MANGOHUD_NOTIFY_H
