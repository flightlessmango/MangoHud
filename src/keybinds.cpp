#include "overlay.h"
#include "timing.hpp"
#include "logging.h"
#include "keybinds.h"
#include "keybinds_libinput.h"

Clock::time_point last_f2_press, toggle_fps_limit_press , last_f12_press, reload_cfg_press, last_upload_press;

#ifdef HAVE_X11
bool x11_keys_are_pressed(const std::vector<KeySym>& keys) {
   if (!init_x11()) {
      return false;
   }

    char keys_return[32];
    size_t pressed = 0;

    g_x11->XQueryKeymap(get_xdisplay(), keys_return);

    for (KeySym ks : keys) {
        KeyCode kc2 = g_x11->XKeysymToKeycode(get_xdisplay(), ks);

        bool isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));

        if (isPressed)
            pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

    return false;
}
#endif //HAVE_X11

#ifdef _WIN32
#include <windows.h>
bool win32_keys_are_pressed(const std::vector<KeySym>& keys) {
    size_t pressed = 0;

    for (KeySym ks : keys) {
        if (GetAsyncKeyState(ks) & 0x8000)
            pressed++;
    }

    if (pressed > 0 && pressed == keys.size()) {
        return true;
    }

    return false;
}
#endif

bool keys_are_pressed(const std::vector<KeySym>& keys) {
#ifdef HAVE_X11
   if (x11_keys_are_pressed(keys))
      return true;
#endif

#ifdef HAVE_LIBINPUT
   if (libinput_keys_are_pressed(keys))
      return true;
#endif

#ifdef _WIN32
   if (win32_keys_are_pressed(keys))
      return true;
#endif

   return false;
}

void check_keybinds(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID){
   using namespace std::chrono_literals;
   auto now = Clock::now(); /* us */
   auto elapsedF2 = now - last_f2_press;
   auto elapsedFpsLimitToggle = now - toggle_fps_limit_press;
   auto elapsedF12 = now - last_f12_press;
   auto elapsedReloadCfg = now - reload_cfg_press;
   auto elapsedUpload = now - last_upload_press;

   auto keyPressDelay = 500ms;

  if (elapsedF2 >= keyPressDelay){
     if (keys_are_pressed(params.toggle_logging) && (now - logger->last_log_end() > 11s)) {
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
      if (keys_are_pressed(params.toggle_fps_limit)){
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
      if (keys_are_pressed(params.toggle_hud)){
         last_f12_press = now;
         params.no_display = !params.no_display;
      }
   }

   if (elapsedReloadCfg >= keyPressDelay){
      if (keys_are_pressed(params.reload_cfg)){
         parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
         reload_cfg_press = now;
      }
   }

   if (params.permit_upload && elapsedUpload >= keyPressDelay){
      if (keys_are_pressed(params.upload_log)){
         last_upload_press = now;
         logger->upload_last_log();
      }
   }
   if (params.permit_upload && elapsedUpload >= keyPressDelay){
      if (keys_are_pressed(params.upload_logs)){
         last_upload_press = now;
         logger->upload_last_logs();
      }
   }
}
