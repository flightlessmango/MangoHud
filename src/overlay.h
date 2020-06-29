#pragma once

#include <string>
#include <stdint.h>
#include <vector>
#include "imgui.h"
#include "overlay_params.h"
#include "iostats.h"
#include "timing.hpp"

struct frame_stat {
   uint64_t stats[OVERLAY_PLOTS_MAX];
};

struct swapchain_stats {
   uint64_t n_frames;
   enum overlay_plots stat_selector;
   double time_dividor;
   struct frame_stat stats_min, stats_max;
   struct frame_stat frames_stats[200];

   ImFont* font1 = nullptr;
   ImFont* font_text = nullptr;
   std::string time;
   double fps;
   struct iostats io;
   int total_cpu;
   uint64_t last_present_time;
   unsigned n_frames_since_update;
   uint64_t last_fps_update;
   ImVec2 main_window_pos;

   struct {
      int32_t major;
      int32_t minor;
      bool is_gles;
   } version_gl;
   struct {
      int32_t major;
      int32_t minor;
      int32_t patch;
   } version_vk;
   std::string engineName;
   std::string engineVersion;
   std::string deviceName;
   std::string gpuName;
   std::string driverName;
};

struct fps_limit {
   Clock::time_point frameStart;
   Clock::time_point frameEnd;
   Clock::duration targetFrameTime;
   Clock::duration frameOverhead;
   Clock::duration sleepTime;
};

struct benchmark_stats {
   float ninety;
   float avg;
   float oneP;
   float pointOneP;
   float total;
   std::vector<float> fps_data;
};

extern struct fps_limit fps_limit_stats;
extern int32_t deviceID;

extern struct benchmark_stats benchmark;

void position_layer(struct swapchain_stats& data, struct overlay_params& params, ImVec2 window_size);
void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, bool is_vulkan);
void update_hud_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID);
void init_gpu_stats(uint32_t& vendorID, overlay_params& params);
void init_cpu_stats(overlay_params& params);
void check_keybinds(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID);
void init_system_info(void);
void FpsLimiter(struct fps_limit& stats);
void imgui_custom_style(struct overlay_params& params);
void get_device_name(int32_t vendorID, int32_t deviceID, struct swapchain_stats& sw_stats);
void calculate_benchmark_data(void);
void create_fonts(const overlay_params& params, ImFont*& small_font, ImFont*& text_font);
