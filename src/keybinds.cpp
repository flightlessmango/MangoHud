#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>
#include <unistd.h>
#include "hud_elements.h"
#include "overlay.h"
#include "timing.hpp"
#include "logging.h"
#include "keybinds.h"
#include "fps_metrics.h"
#include "fps_limiter.h"

Clock::time_point last_f2_press, toggle_fps_limit_press, toggle_preset_press, last_f12_press, reload_cfg_press, last_upload_press;

void check_keybinds(struct overlay_params& params){
   auto real_params = get_params();
   using namespace std::chrono_literals;
   auto now = Clock::now(); /* us */
   auto elapsedF2 = now - last_f2_press;
   auto elapsedFpsLimitToggle = now - toggle_fps_limit_press;
   auto elapsedPresetToggle = now - toggle_preset_press;
   auto elapsedF12 = now - last_f12_press;
   auto elapsedReloadCfg = now - reload_cfg_press;
   auto elapsedUpload = now - last_upload_press;

   static Clock::time_point last_check;
   if (now - last_check < 100ms)
      return;
   last_check = now;

   const auto keyPressDelay = 400ms;

   if (elapsedF2 >= keyPressDelay &&
       keys_are_pressed(real_params->toggle_logging)) {
      last_f2_press = now;
      if (logger->is_active()) {
         logger->stop_logging();
      } else {
         logger->start_logging();
         benchmark.fps_data.clear();
      }
   }

   if (elapsedFpsLimitToggle >= keyPressDelay &&
       keys_are_pressed(real_params->toggle_fps_limit)) {
      toggle_fps_limit_press = now;
      fps_limiter->next_limit();
   }

   if (elapsedPresetToggle >= keyPressDelay &&
       keys_are_pressed(real_params->toggle_preset)) {
     toggle_preset_press = now;
     size_t size = real_params->preset.size();
     for (size_t i = 0; i < size; i++){
       if(real_params->preset[i] == current_preset) {
         current_preset = real_params->preset[++i%size];
         parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), true);
         break;
       }
     }
   }

   if (elapsedF12 >= keyPressDelay &&
       keys_are_pressed(real_params->toggle_hud)) {
      last_f12_press = now;
      printf("no display toggle\n");
      real_params->no_display = !real_params->no_display;
   }

   if (elapsedReloadCfg >= keyPressDelay &&
       keys_are_pressed(real_params->reload_cfg)) {
      parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
      reload_cfg_press = now;
   }

   if (real_params->permit_upload && elapsedUpload >= keyPressDelay &&
       keys_are_pressed(real_params->upload_log)) {
      last_upload_press = now;
      logger->upload_last_log();
   }

   if (real_params->permit_upload && elapsedUpload >= keyPressDelay &&
       keys_are_pressed(real_params->upload_logs)) {
      last_upload_press = now;
      logger->upload_last_logs();
   }

   if (elapsedF12 >= keyPressDelay &&
       keys_are_pressed(real_params->toggle_hud_position)) {
      next_hud_position();
      last_f12_press = now;
   }

   if (elapsedF12 >= keyPressDelay &&
       keys_are_pressed(real_params->reset_fps_metrics)) {
      last_f12_press = now;
      if (fpsmetrics)
         fpsmetrics->reset_metrics();
   }
}
