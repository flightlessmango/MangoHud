#include <cstdlib>
#include <functional>
#include <thread>
#include <iostream>
#include "imgui_hud_shared.h"

#ifdef HAVE_DBUS
#include "dbus_info.h"
#endif

namespace MangoHud { namespace GL {

notify_thread notifier;
static bool cfg_inited = false;
ImVec2 window_size;
bool inited = false;
overlay_params params {};

// seems to quit by itself though
static std::unique_ptr<notify_thread, std::function<void(notify_thread *)>>
    stop_it(&notifier, [](notify_thread *n){ stop_notifier(*n); });

void imgui_init()
{
    if (cfg_inited)
        return;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
    notifier.params = &params;
    start_notifier(notifier);
    window_size = ImVec2(params.width, params.height);
    init_system_info();
    cfg_inited = true;
    init_cpu_stats(params);
}

}} // namespaces
