#pragma once
#ifndef MANGOHUD_OVERLAY_H
#define MANGOHUD_OVERLAY_H

#include <string>
#include <stdint.h>
#include <vector>
#include <deque>
#include <imgui.h>
#include "overlay_params.h"
#include "iostats.h"
#include "timing.hpp"
#include "hud_elements.h"
#include "version.h"
#include "gpu.h"
#include "logging.h"
#include "vk_enum_to_str.h"
#include "notify.h"
#include <vulkan/vk_layer.h>
#include "amdgpu.h"

#ifdef HAVE_DBUS
#include "dbus_info.h"
extern float g_overflow;
#endif
struct frame_stat {
   uint64_t stats[OVERLAY_PLOTS_MAX];
};

static const int kMaxGraphEntries = 50;

enum EngineTypes
{
   UNKNOWN,

   OPENGL,
   VULKAN,

   DXVK,
   VKD3D,
   DAMAVAND,
   ZINK,

   WINED3D,
   FERAL3D,
   TOGL,

   GAMESCOPE
};

extern const char* engines[];

struct swapchain_stats {
   uint64_t n_frames;
   enum overlay_plots stat_selector;
   double time_dividor;
   struct frame_stat stats_min, stats_max;
   struct frame_stat frames_stats[200];

   ImFont* font1 = nullptr;
   ImFont* font_text = nullptr;
   size_t font_params_hash = 0;
   std::string time;
   double fps;
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
   enum EngineTypes engine;
};

struct fps_limit {
   Clock::time_point frameStart;
   Clock::time_point frameEnd;
   Clock::duration targetFrameTime;
   Clock::duration frameOverhead;
   Clock::duration sleepTime;
};

struct benchmark_stats {
   float total;
   std::vector<float> fps_data;
   std::vector<std::pair<std::string, float>> percentile_data;
};

struct LOAD_DATA {
   ImVec4 color_low;
   ImVec4 color_med;
   ImVec4 color_high;
   unsigned med_load;
   unsigned high_load;
};

/* Mapped from VkInstace/VkPhysicalDevice */
struct instance_data {
   struct vk_instance_dispatch_table vtable;
   VkInstance instance;
   struct overlay_params params;
   uint32_t api_version;
   string engineName, engineVersion;
   enum EngineTypes engine;
   notify_thread notifier;
   int control_client;
};

/* Mapped from VkDevice */
struct queue_data;
struct device_data {
   struct instance_data *instance;

   PFN_vkSetDeviceLoaderData set_device_loader_data;

   struct vk_device_dispatch_table vtable;
   VkPhysicalDevice physical_device;
   VkDevice device;

   VkPhysicalDeviceProperties properties;

   struct queue_data *graphic_queue;

   std::vector<struct queue_data *> queues;
};

extern struct fps_limit fps_limit_stats;
extern int32_t deviceID;

extern struct benchmark_stats benchmark;
extern ImVec2 real_font_size;
extern std::string wineVersion;
extern std::deque<logData> graph_data;
extern overlay_params *_params;
extern double min_frametime, max_frametime;
extern bool steam_focused;

void position_layer(struct swapchain_stats& data, struct overlay_params& params, ImVec2 window_size);
void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, bool is_vulkan);
void update_hud_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID);
void update_hud_info_with_frametime(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID, uint64_t frametime_ns);
void update_hw_info(struct overlay_params& params, uint32_t vendorID);
void init_gpu_stats(uint32_t& vendorID, uint32_t reported_deviceID, overlay_params& params);
void init_cpu_stats(overlay_params& params);
void check_keybinds(overlay_params& params, uint32_t vendorID);
void init_system_info(void);
void FpsLimiter(struct fps_limit& stats);
std::string get_device_name(int32_t vendorID, int32_t deviceID);
void calculate_benchmark_data(overlay_params* params);
void create_fonts(const overlay_params& params, ImFont*& small_font, ImFont*& text_font);
void right_aligned_text(ImVec4& col, float off_x, const char *fmt, ...);
void center_text(const std::string& text);
ImVec4 change_on_load_temp(LOAD_DATA& data, unsigned current);
float get_time_stat(void *_data, int _idx);
void stop_hw_updater();
extern void control_client_check(struct device_data *device_data);
extern void process_control_socket(struct instance_data *instance_data);

#ifdef HAVE_DBUS
void render_mpris_metadata(overlay_params& params, mutexed_metadata& meta, uint64_t frame_timing);
#endif

#endif //MANGOHUD_OVERLAY_H
