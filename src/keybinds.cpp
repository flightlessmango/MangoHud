#include "overlay.h"
#include "timing.hpp"
#include "logging.h"
#include "keybinds.h"

void check_keybinds(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID){
   using namespace std::chrono_literals;
   bool pressed = false; // FIXME just a placeholder until wayland support
   auto now = Clock::now(); /* us */
   auto elapsedF2 = now - last_f2_press;
   auto elapsedFpsLimitToggle = now - toggle_fps_limit_press;
   auto elapsedF12 = now - last_f12_press;
   auto elapsedReloadCfg = now - reload_cfg_press;
   auto elapsedUpload = now - last_upload_press;

   static Clock::time_point last_check;
   if (now - last_check < 100ms)
      return;
   last_check = now;

   auto keyPressDelay = 400ms;

  if (elapsedF2 >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
     pressed = keys_are_pressed(params.toggle_logging);
#else
     pressed = false;
#endif
     if (pressed) {
       last_f2_press = now;
       if (logger->is_active()) {
         logger->stop_logging();
       } else {
         logger->start_logging();
         std::thread(update_hw_info, std::ref(sw_stats), std::ref(params),
                     vendorID)
             .detach();
         benchmark.fps_data.clear();
       }
     }
   }

  if (elapsedFpsLimitToggle >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
      pressed = keys_are_pressed(params.toggle_fps_limit);
#else
      pressed = false;
#endif
      if (pressed){
         toggle_fps_limit_press = now;
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
   }

   if (elapsedF12 >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
      pressed = keys_are_pressed(params.toggle_hud);
#else
      pressed = false;
#endif
      if (pressed){
         last_f12_press = now;
         params.no_display = !params.no_display;
      }
   }

   if (elapsedReloadCfg >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
      pressed = keys_are_pressed(params.reload_cfg);
#else
      pressed = false;
#endif
      if (pressed){
         parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
         _params = &params;
         reload_cfg_press = now;
      }
   }

   if (params.permit_upload && elapsedUpload >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
      pressed = keys_are_pressed(params.upload_log);
#else
      pressed = false;
#endif
      if (pressed){
         last_upload_press = now;
         logger->upload_last_log();
      }
   }
   if (params.permit_upload && elapsedUpload >= keyPressDelay){
#if defined(HAVE_X11) || defined(_WIN32)
      pressed = keys_are_pressed(params.upload_logs);
#else
      pressed = false;
#endif
      if (pressed){
         last_upload_press = now;
         logger->upload_last_logs();
      }
   }
}
