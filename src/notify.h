#include <thread>
#include "overlay_params.h"

struct notify_thread
{
    overlay_params *params = nullptr;
    bool quit = false;
};

extern pthread_t fileChange;
extern void *fileChanged(void *params_void);