#include "d3d_shared.h"

bool cfg_inited = false;
ImVec2 window_size;
overlay_params params {};
struct swapchain_stats sw_stats {};
uint32_t vendorID;

void init_d3d_shared(){
    vendorID = get_device_id_dxgi();
    if (cfg_inited)
        return;
     parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
     cfg_inited = true;
    //  init_cpu_stats(params);
}