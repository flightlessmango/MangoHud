#include "d3d_shared.h"
#include "overlay.h"

bool cfg_inited = false;
ImVec2 window_size;
overlay_params params {};
struct swapchain_stats sw_stats {};
uint32_t vendorID;
kiero::RenderType::Enum dx_version;

void init_d3d_shared(){
    if (!logger) logger = std::make_unique<Logger>(&params);
    vendorID = get_device_id_dxgi();
    if (cfg_inited)
        return;
     parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
     _params = params;
     cfg_inited = true;
    //  init_cpu_stats(params);
}

void d3d_run(){
    check_keybinds(sw_stats, params, vendorID);
	update_hud_info(sw_stats, params, vendorID);
}