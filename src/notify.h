#include <thread>
#include <mutex>
#include "overlay_params.h"

struct notify_thread
{
    int fd = -1, wd = -1;
    overlay_params *params = nullptr;
    bool quit = false;
    std::mutex mutex;
    std::thread thread;
};

bool start_notifier(notify_thread& nt);
void stop_notifier(notify_thread& nt);
