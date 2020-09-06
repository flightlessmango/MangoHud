#include "overlay.h"
#include "timing.hpp"
#include "logging.h"
#include "keybinds.h"

void check_keybinds(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID){
   using namespace std::chrono_literals;
   bool pressed = false; // FIXME just a placeholder until wayland support
   auto now = Clock::now(); /* us */
   auto elapsedF2 = now - last_f2_press;
   auto elapsedF12 = now - last_f12_press;
   auto elapsedReloadCfg = now - reload_cfg_press;
   auto elapsedUpload = now - last_upload_press;

   auto keyPressDelay = 500ms;

  if (elapsedF2 >= keyPressDelay){
#ifdef HAVE_X11
     pressed = keys_are_pressed(params.toggle_logging);
#else
     pressed = false;
#endif
     if (pressed && (now - logger->last_log_end() > 11s)) {
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

   if (elapsedF12 >= keyPressDelay){
#ifdef HAVE_X11
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
#ifdef HAVE_X11
      pressed = keys_are_pressed(params.reload_cfg);
#else
      pressed = false;
#endif
      if (pressed){
         parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
         reload_cfg_press = now;
      }
   }

   if (params.permit_upload && elapsedUpload >= keyPressDelay){
#ifdef HAVE_X11
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
#ifdef HAVE_X11
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