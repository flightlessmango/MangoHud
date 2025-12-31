#pragma once
#include <imgui.h>
#include <string>

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
   double time_dividor;

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

void init_spdlog();
const char* engine_name(const swapchain_stats& sw_stats);
