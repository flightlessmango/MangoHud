#include "../overlay.h"

extern bool cfg_inited;
extern ImVec2 window_size;
extern struct overlay_params params;
extern struct swapchain_stats sw_stats;
extern uint32_t vendorID;

extern void init_d3d_shared(void);
extern void d3d_run(void);
extern uint32_t get_device_id_dxgi(void);