#include "overlay.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <string>
#include "vulkan.h"

void init_spdlog()
{
   if (spdlog::get("MANGOHUD"))
      return;

   spdlog::set_default_logger(spdlog::stderr_color_mt("MANGOHUD")); // Just to get the name in log
   if (getenv("MANGOHUD_USE_LOGFILE"))
   {
      try
      {
         // Not rotating when opening log as proton/wine create multiple (sub)processes
         auto log = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (get_config_dir() + "/MangoHud/MangoHud.log", 10*1024*1024, 5, false);
         spdlog::get("MANGOHUD")->sinks().push_back(log);
      }
      catch (const spdlog::spdlog_ex &ex)
      {
         SPDLOG_ERROR("{}", ex.what());
      }
   }
#ifdef DEBUG
   spdlog::set_level(spdlog::level::level_enum::debug);
#endif
   spdlog::cfg::load_env_levels();

   // Use MANGOHUD_LOG_LEVEL to correspond to SPDLOG_LEVEL
   if (getenv("MANGOHUD_LOG_LEVEL")) {
      std::string log_level = getenv("MANGOHUD_LOG_LEVEL");
      std::vector<std::string> levels;
      levels = {"trace","debug","info","warning","error","critical","off"};
      for (auto & element : levels) {
         transform(log_level.begin(), log_level.end(), log_level.begin(), ::tolower);
         if(log_level == element ) {
            spdlog::set_level(spdlog::level::from_str(log_level));
         }
      }
#ifndef DEBUG
   } else {
      std::string log_level = "info";
      transform(log_level.begin(), log_level.end(), log_level.begin(), ::tolower);
      spdlog::set_level(spdlog::level::from_str(log_level));
#endif
   }

}


const char* engine_name(const swapchain_stats& sw_stats) {
   const char* engines[] = {
      "Unknown", "OpenGL", "VULKAN", "DXVK", "VKD3D", "DAMAVAND",
      "ZINK", "WINED3D", "Feral3D", "ToGL", "GAMESCOPE"
   };
   const char* engines_short[] = {
      "Unknown", "OGL", "VK", "DXVK", "VKD3D", "DV",
       "ZINK", "WD3D", "Feral3D", "ToGL", "GS"
   };

   auto engine = sw_stats.engine;
//    auto params = get_params();
//    if (!params)
//       return "Unknown";

//    if (!params->fps_text.empty())
//       return params->fps_text.c_str();

//    auto& en = params->enabled;

//    if (en[OVERLAY_PARAM_ENABLED_hide_engine_names]) {
//       en[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = true;
//       return "FPS";
//    }

//    if (en[OVERLAY_PARAM_ENABLED_horizontal] && !en[OVERLAY_PARAM_ENABLED_engine_short_names]) {
//       en[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = true;
//       return "FPS";
//    }

//    if (en[OVERLAY_PARAM_ENABLED_dx_api]) {
//       if (engine == EngineTypes::VKD3D)
//          return "DX12";

//       if (engine == EngineTypes::DXVK) {
//          if (sw_stats.applicationVersion == 1)
//             return "DX9";

//          if (sw_stats.applicationVersion == 2)
//             return "DX11";

//          return "DX?";
//       }
//    }

   return engines[engine];
}
