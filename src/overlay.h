#include <string>
#include <stdint.h>
#include "imgui.h"
#include "overlay_params.h"
#include "iostats.h"

extern std::string engineName;
struct frame_stat {
   uint64_t stats[OVERLAY_PARAM_ENABLED_MAX];
};

struct swapchain_stats {
   uint64_t n_frames;
   enum overlay_param_enabled stat_selector;
   double time_dividor;
   struct frame_stat stats_min, stats_max;
   struct frame_stat frames_stats[200];

   ImFont* font1 = nullptr;
   std::string time;
   double fps;
   struct iostats io;
   int total_cpu;
   uint64_t last_present_time;
   unsigned n_frames_since_update;
   uint64_t last_fps_update;
};

struct fps_limit {
   int64_t frameStart;
   int64_t frameEnd;
   int64_t targetFrameTime;
   int64_t frameOverhead;
   int64_t sleepTime;
};

void position_layer(struct overlay_params& params, ImVec2 window_size, unsigned width, unsigned height);
void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, unsigned width, unsigned height);
void update_hud_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID);
void init_gpu_stats(uint32_t& vendorID, overlay_params& params);
void check_keybinds(struct overlay_params& params);
void init_system_info(void);
void FpsLimiter(struct fps_limit& stats);
void imgui_custom_style(void);