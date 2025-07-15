#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <unistd.h>
#include "overlay.h"
#include "timing.hpp"
#include "logging.h"
#include "keybinds.h"
#include "fps_metrics.h"

bool prev_toggle_logging = false;
bool prev_toggle_fps_limit = false;
bool prev_toggle_preset = false;
bool prev_toggle_hud = false;
bool prev_reload_cfg = false;
bool prev_upload_log = false;
bool prev_upload_logs = false;
bool prev_toggle_hud_position = false;
bool prev_reset_fps_metrics = false;

void check_keybinds(struct overlay_params& params){
   using namespace std::chrono_literals;

   bool value;

   if ((value = keys_are_pressed(params.toggle_logging)) && value != prev_toggle_logging) {
      if (logger->is_active()) {
         logger->stop_logging();
      } else {
         logger->start_logging();
         benchmark.fps_data.clear();
      }
   }

   prev_toggle_logging = value;

   if ((value = keys_are_pressed(params.toggle_fps_limit)) && value != prev_toggle_fps_limit) {
      for (size_t i = 0; i < params.fps_limit.size(); i++){
         uint32_t fps_limit = params.fps_limit[i];
         // current fps limit equals vector entry, use next / first
         if((fps_limit > 0 && fps_limit_stats.targetFrameTime == std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1) / params.fps_limit[i]))
               || (fps_limit == 0 && fps_limit_stats.targetFrameTime == fps_limit_stats.targetFrameTime.zero())) {
            uint32_t newFpsLimit = i+1 == params.fps_limit.size() ? params.fps_limit[0] : params.fps_limit[i+1];
            if(newFpsLimit > 0) {
               fps_limit_stats.targetFrameTime = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1) / newFpsLimit);
            } else {
               fps_limit_stats.targetFrameTime = {};
            }
            break;
         }
      }
   }

   prev_toggle_fps_limit = value;

   if ((value = keys_are_pressed(params.toggle_preset)) && value != prev_toggle_preset) {
     size_t size = params.preset.size();
     for (size_t i = 0; i < size; i++){
       if(params.preset[i] == current_preset) {
         current_preset = params.preset[++i%size];
         parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), true);
         break;
       }
     }
   }

   prev_toggle_preset = value;

   if ((value = keys_are_pressed(params.toggle_hud)) && value != prev_toggle_hud) {
      params.no_display = !params.no_display;
   }

   prev_toggle_hud = value;

   if ((value = keys_are_pressed(params.reload_cfg)) && value != prev_reload_cfg) {
      parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
      _params = &params;
   }

   prev_reload_cfg = value;

   if ((value = keys_are_pressed(params.upload_log)) && params.permit_upload && value != prev_upload_log) {
      logger->upload_last_log();
   }

   prev_upload_log = value;

   if ((value = keys_are_pressed(params.upload_logs)) && params.permit_upload && value != prev_upload_logs) {
      logger->upload_last_logs();
   }

   prev_upload_logs = value;

   if ((value = keys_are_pressed(params.toggle_hud_position)) && value != prev_toggle_hud_position) {
      next_hud_position(params);
   }

   prev_toggle_hud_position = value;

   if ((value = keys_are_pressed(params.reset_fps_metrics)) && value != prev_reset_fps_metrics) {
      if (fpsmetrics)
         fpsmetrics->reset_metrics();
   }

   prev_reset_fps_metrics = value;
}
