#pragma once
#ifndef MANGOHUD_OVERLAY_H
#define MANGOHUD_OVERLAY_H

#include <string>
#include <stdint.h>
#include <vector>
#include <deque>
#include <imgui.h>
#include "imgui_internal.h"
#include "overlay_params.h"
#include "hud_elements.h"

#include "dbus_info.h"
#include "logging.h"

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

struct swapchain_stats {
   uint64_t n_frames;
   enum overlay_plots stat_selector;
   double time_dividor;
   struct frame_stat stats_min, stats_max;
   struct frame_stat frames_stats[200];

   ImFont* font_small = nullptr;
   ImFont* font_text = nullptr;
   ImFont* font_secondary = nullptr;
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
   uint32_t applicationVersion;
   enum EngineTypes engine;
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


inline const char* engine_name(const swapchain_stats& sw_stats) {
   const char* engines[] = {
      "Unknown", "OpenGL", "VULKAN", "DXVK", "VKD3D", "DAMAVAND",
      "ZINK", "WINED3D", "Feral3D", "ToGL", "GAMESCOPE"
   };
   const char* engines_short[] = {
      "Unknown", "OGL", "VK", "DXVK", "VKD3D", "DV",
       "ZINK", "WD3D", "Feral3D", "ToGL", "GS"
   };

   auto engine = sw_stats.engine;
   auto params = get_params();
   if (!params)
      return "Unknown";

   if (!params->fps_text.empty())
      return params->fps_text.c_str();

   auto& en = params->enabled;

   if (en[OVERLAY_PARAM_ENABLED_hide_engine_names]) {
      en[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = true;
      return "FPS";
   }

   if (en[OVERLAY_PARAM_ENABLED_horizontal] && !en[OVERLAY_PARAM_ENABLED_engine_short_names]) {
      en[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = true;
      return "FPS";
   }

   if (en[OVERLAY_PARAM_ENABLED_dx_api]) {
      if (engine == EngineTypes::VKD3D)
         return "DX12";

      if (engine == EngineTypes::DXVK) {
         if (sw_stats.applicationVersion == 1)
            return "DX9";

         if (sw_stats.applicationVersion == 2)
            return "DX11";

         return "DX?";
      }
   }

   return en[OVERLAY_PARAM_ENABLED_engine_short_names] ? engines_short[engine] : engines[engine];
}

extern uint32_t deviceID;

extern struct benchmark_stats benchmark;
extern ImVec2 real_font_size;
extern std::string wineVersion;
extern std::deque<logData> graph_data;
extern double min_frametime, max_frametime;
extern bool steam_focused;
extern int fan_speed;
extern int current_preset;
extern std::vector<float> frametime_data;

void init_spdlog();
void overlay_new_frame(const struct overlay_params& params);
void overlay_end_frame();
void position_layer(struct swapchain_stats& data, const struct overlay_params& params, const ImVec2& window_size);
void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, bool is_vulkan);
void update_hud_info(struct swapchain_stats& sw_stats, const struct overlay_params& params, uint32_t vendorID);
void update_hud_info_with_frametime(struct swapchain_stats& sw_stats, const struct overlay_params& params, uint32_t vendorID, uint64_t frametime_ns);
void update_hw_info(const struct overlay_params& params, uint32_t vendorID);
void init_cpu_stats(overlay_params& params);
void check_keybinds(overlay_params& params);
void init_system_info(void);
void check_for_vkbasalt_and_gamemode();
void create_fonts(ImFontAtlas* font_atlas, const overlay_params& params, ImFont*& small_font, ImFont*& text_font, ImFont*& secondary_font);
void right_aligned_text(ImVec4& col, float off_x, const char *fmt, ...);
void center_text(const std::string& text);
ImVec4 change_on_load_temp(LOAD_DATA& data, unsigned current);
float get_time_stat(void *_data, int _idx);
void stop_hw_updater();
extern void control_client_check(int control, int& control_client, const std::string& deviceName);
extern void process_control_socket(int& control_client, overlay_params &params);
extern void control_send(int control_client, const char *cmd, unsigned cmdlen, const char *param, unsigned paramlen);
extern int global_control_client;
#ifdef HAVE_DBUS
void render_mpris_metadata(const overlay_params& params, mutexed_metadata& meta, uint64_t frame_timing);
#endif
void update_fan();
void next_hud_position();
void horizontal_separator(struct overlay_params& params);
void RenderOutlinedText(const char* text, ImU32 textColor);
#endif //MANGOHUD_OVERLAY_H
