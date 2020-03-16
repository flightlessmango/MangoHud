#include <thread>
#include <mutex>
#include "overlay_params.h"

struct notify_thread
{
    overlay_params *params = nullptr;
    bool quit = false;
    std::mutex mutex;
};

extern pthread_t fileChange;
extern void *fileChanged(void *params_void);