#include <cstdlib>
#include <functional>
#include <thread>
#include <iostream>
#include "imgui_hud_shared.h"
#include "dbus_info.h"

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
    if (params.enabled[OVERLAY_PARAM_ENABLED_media_player]) {
        try {
            dbusmgr::dbus_mgr.init();
            get_spotify_metadata(dbusmgr::dbus_mgr, spotify);
        } catch (std::runtime_error& e) {
            std::cerr << "Failed to get initial Spotify metadata: " << e.what() << std::endl;
        }
    }
}

}} // namespaces
