#pragma once
#ifndef MANGOHUD_OVERLAY_PARAMS_H
#define MANGOHUD_OVERLAY_PARAMS_H

#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KeySym
typedef unsigned long KeySym;
#endif

#define RGBGetBValue(rgb)   (rgb & 0x000000FF)
#define RGBGetGValue(rgb)   ((rgb >> 8) & 0x000000FF)
#define RGBGetRValue(rgb)   ((rgb >> 16) & 0x000000FF)

#define ToRGBColor(r, g, b, a) ((r << 16) | (g << 8) | (b));

#define OVERLAY_PARAMS                               \
   OVERLAY_PARAM_BOOL(fps)                           \
   OVERLAY_PARAM_BOOL(frame_timing)                  \
   OVERLAY_PARAM_BOOL(core_load)                     \
   OVERLAY_PARAM_BOOL(cpu_temp)                      \
   OVERLAY_PARAM_BOOL(cpu_power)                     \
   OVERLAY_PARAM_BOOL(gpu_temp)                      \
   OVERLAY_PARAM_BOOL(cpu_stats)                     \
   OVERLAY_PARAM_BOOL(gpu_stats)                     \
   OVERLAY_PARAM_BOOL(ram)                           \
   OVERLAY_PARAM_BOOL(swap)                          \
   OVERLAY_PARAM_BOOL(vram)                          \
   OVERLAY_PARAM_BOOL(procmem)                       \
   OVERLAY_PARAM_BOOL(procmem_shared)                \
   OVERLAY_PARAM_BOOL(procmem_virt)                  \
   OVERLAY_PARAM_BOOL(time)                          \
   OVERLAY_PARAM_BOOL(full)                          \
   OVERLAY_PARAM_BOOL(read_cfg)                      \
   OVERLAY_PARAM_BOOL(io_read)                       \
   OVERLAY_PARAM_BOOL(io_write)                      \
   OVERLAY_PARAM_BOOL(io_stats)                      \
   OVERLAY_PARAM_BOOL(gpu_mem_clock)                 \
   OVERLAY_PARAM_BOOL(gpu_core_clock)                \
   OVERLAY_PARAM_BOOL(gpu_power)                     \
   OVERLAY_PARAM_BOOL(arch)                          \
   OVERLAY_PARAM_BOOL(media_player)                  \
   OVERLAY_PARAM_BOOL(version)                       \
   OVERLAY_PARAM_BOOL(vulkan_driver)                 \
   OVERLAY_PARAM_BOOL(gpu_name)                      \
   OVERLAY_PARAM_BOOL(engine_version)                \
   OVERLAY_PARAM_BOOL(histogram)                     \
   OVERLAY_PARAM_BOOL(wine)                          \
   OVERLAY_PARAM_BOOL(gpu_load_change)               \
   OVERLAY_PARAM_BOOL(cpu_load_change)               \
   OVERLAY_PARAM_BOOL(core_load_change)              \
   OVERLAY_PARAM_BOOL(graphs)                        \
   OVERLAY_PARAM_BOOL(legacy_layout)                 \
   OVERLAY_PARAM_BOOL(cpu_mhz)                       \
   OVERLAY_PARAM_BOOL(frametime)                     \
   OVERLAY_PARAM_BOOL(frame_count)                   \
   OVERLAY_PARAM_BOOL(resolution)                    \
   OVERLAY_PARAM_BOOL(show_fps_limit)                \
   OVERLAY_PARAM_BOOL(fps_color_change)              \
   OVERLAY_PARAM_BOOL(custom_text_center)            \
   OVERLAY_PARAM_BOOL(custom_text)                   \
   OVERLAY_PARAM_CUSTOM(background_image)            \
   OVERLAY_PARAM_CUSTOM(image)                       \
   OVERLAY_PARAM_CUSTOM(image_max_width)             \
   OVERLAY_PARAM_BOOL(exec)                          \
   OVERLAY_PARAM_BOOL(vkbasalt)                      \
   OVERLAY_PARAM_BOOL(gamemode)                      \
   OVERLAY_PARAM_BOOL(battery)                       \
   OVERLAY_PARAM_BOOL(battery_icon)                  \
   OVERLAY_PARAM_BOOL(fps_only)                      \
   OVERLAY_PARAM_BOOL(fsr)                           \
   OVERLAY_PARAM_BOOL(mangoapp_steam)                \
   OVERLAY_PARAM_BOOL(debug)                         \
   OVERLAY_PARAM_BOOL(gamepad_battery)               \
   OVERLAY_PARAM_BOOL(gamepad_battery_icon)          \
   OVERLAY_PARAM_BOOL(hide_fsr_sharpness)            \
   OVERLAY_PARAM_BOOL(fan)                           \
   OVERLAY_PARAM_BOOL(throttling_status)             \
   OVERLAY_PARAM_BOOL(fcat)                          \
   OVERLAY_PARAM_CUSTOM(fps_sampling_period)         \
   OVERLAY_PARAM_CUSTOM(output_folder)               \
   OVERLAY_PARAM_CUSTOM(output_file)                 \
   OVERLAY_PARAM_CUSTOM(font_file)                   \
   OVERLAY_PARAM_CUSTOM(font_file_text)              \
   OVERLAY_PARAM_CUSTOM(font_glyph_ranges)           \
   OVERLAY_PARAM_CUSTOM(no_small_font)               \
   OVERLAY_PARAM_CUSTOM(font_size)                   \
   OVERLAY_PARAM_CUSTOM(font_size_text)              \
   OVERLAY_PARAM_CUSTOM(font_scale)                  \
   OVERLAY_PARAM_CUSTOM(font_scale_media_player)     \
   OVERLAY_PARAM_CUSTOM(position)                    \
   OVERLAY_PARAM_CUSTOM(width)                       \
   OVERLAY_PARAM_CUSTOM(height)                      \
   OVERLAY_PARAM_CUSTOM(no_display)                  \
   OVERLAY_PARAM_CUSTOM(control)                     \
   OVERLAY_PARAM_CUSTOM(fps_limit)                   \
   OVERLAY_PARAM_CUSTOM(vsync)                       \
   OVERLAY_PARAM_CUSTOM(gl_vsync)                    \
   OVERLAY_PARAM_CUSTOM(gl_size_query)               \
   OVERLAY_PARAM_CUSTOM(gl_bind_framebuffer)         \
   OVERLAY_PARAM_CUSTOM(gl_dont_flip)                \
   OVERLAY_PARAM_CUSTOM(toggle_hud)                  \
   OVERLAY_PARAM_CUSTOM(toggle_fps_limit)            \
   OVERLAY_PARAM_CUSTOM(toggle_logging)              \
   OVERLAY_PARAM_CUSTOM(reload_cfg)                  \
   OVERLAY_PARAM_CUSTOM(upload_log)                  \
   OVERLAY_PARAM_CUSTOM(upload_logs)                 \
   OVERLAY_PARAM_CUSTOM(offset_x)                    \
   OVERLAY_PARAM_CUSTOM(offset_y)                    \
   OVERLAY_PARAM_CUSTOM(background_alpha)            \
   OVERLAY_PARAM_CUSTOM(time_format)                 \
   OVERLAY_PARAM_CUSTOM(io_read)                     \
   OVERLAY_PARAM_CUSTOM(io_write)                    \
   OVERLAY_PARAM_CUSTOM(cpu_color)                   \
   OVERLAY_PARAM_CUSTOM(gpu_color)                   \
   OVERLAY_PARAM_CUSTOM(vram_color)                  \
   OVERLAY_PARAM_CUSTOM(ram_color)                   \
   OVERLAY_PARAM_CUSTOM(engine_color)                \
   OVERLAY_PARAM_CUSTOM(frametime_color)             \
   OVERLAY_PARAM_CUSTOM(background_color)            \
   OVERLAY_PARAM_CUSTOM(io_color)                    \
   OVERLAY_PARAM_CUSTOM(text_color)                  \
   OVERLAY_PARAM_CUSTOM(wine_color)                  \
   OVERLAY_PARAM_CUSTOM(battery_color)               \
   OVERLAY_PARAM_CUSTOM(alpha)                       \
   OVERLAY_PARAM_CUSTOM(log_duration)                \
   OVERLAY_PARAM_CUSTOM(pci_dev)                     \
   OVERLAY_PARAM_CUSTOM(media_player_name)           \
   OVERLAY_PARAM_CUSTOM(media_player_color)          \
   OVERLAY_PARAM_CUSTOM(media_player_format)         \
   OVERLAY_PARAM_CUSTOM(cpu_text)                    \
   OVERLAY_PARAM_CUSTOM(gpu_text)                    \
   OVERLAY_PARAM_CUSTOM(log_interval)                \
   OVERLAY_PARAM_CUSTOM(permit_upload)               \
   OVERLAY_PARAM_CUSTOM(benchmark_percentiles)       \
   OVERLAY_PARAM_CUSTOM(help)                        \
   OVERLAY_PARAM_CUSTOM(gpu_load_value)              \
   OVERLAY_PARAM_CUSTOM(cpu_load_value)              \
   OVERLAY_PARAM_CUSTOM(gpu_load_color)              \
   OVERLAY_PARAM_CUSTOM(cpu_load_color)              \
   OVERLAY_PARAM_CUSTOM(fps_value)                   \
   OVERLAY_PARAM_CUSTOM(fps_color)                   \
   OVERLAY_PARAM_CUSTOM(cellpadding_y)               \
   OVERLAY_PARAM_CUSTOM(table_columns)               \
   OVERLAY_PARAM_CUSTOM(blacklist)                   \
   OVERLAY_PARAM_CUSTOM(autostart_log)               \
   OVERLAY_PARAM_CUSTOM(round_corners)               \
   OVERLAY_PARAM_CUSTOM(fsr_steam_sharpness)         \
   OVERLAY_PARAM_CUSTOM(fcat_screen_edge)            \
   OVERLAY_PARAM_CUSTOM(fcat_overlay_width)          \

enum overlay_param_position {
   LAYER_POSITION_TOP_LEFT,
   LAYER_POSITION_TOP_RIGHT,
   LAYER_POSITION_MIDDLE_LEFT,
   LAYER_POSITION_MIDDLE_RIGHT,
   LAYER_POSITION_BOTTOM_LEFT,
   LAYER_POSITION_BOTTOM_RIGHT,
   LAYER_POSITION_TOP_CENTER,
};

enum overlay_plots {
    OVERLAY_PLOTS_frame_timing,
    OVERLAY_PLOTS_MAX,
};

enum font_glyph_ranges {
   FG_KOREAN                  = (1u << 0),
   FG_CHINESE_FULL            = (1u << 1),
   FG_CHINESE_SIMPLIFIED      = (1u << 2),
   FG_JAPANESE                = (1u << 3),
   FG_CYRILLIC                = (1u << 4),
   FG_THAI                    = (1u << 5),
   FG_VIETNAMESE              = (1u << 6),
   FG_LATIN_EXT_A             = (1u << 7),
   FG_LATIN_EXT_B             = (1u << 8),
};

enum gl_size_query {
   GL_SIZE_DRAWABLE,
   GL_SIZE_VIEWPORT,
   GL_SIZE_SCISSORBOX, // needed?
};

enum overlay_param_enabled {
#define OVERLAY_PARAM_BOOL(name) OVERLAY_PARAM_ENABLED_##name,
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
   OVERLAY_PARAM_ENABLED_MAX
};

struct overlay_params {
   bool enabled[OVERLAY_PARAM_ENABLED_MAX];
   enum overlay_param_position position;
   int control;
   uint32_t fps_sampling_period; /* ns */
   std::vector<std::uint32_t> fps_limit;
   bool help;
   bool no_display;
   bool full;
   bool io_read, io_write, io_stats;
   unsigned width;
   unsigned height;
   int offset_x, offset_y;
   float round_corners;
   unsigned vsync;
   int gl_vsync;
   int gl_bind_framebuffer {-1};
   enum gl_size_query gl_size_query {GL_SIZE_DRAWABLE};
   bool gl_dont_flip {false};
   uint64_t log_duration;
   unsigned cpu_color, gpu_color, vram_color, ram_color, engine_color, io_color, frametime_color, background_color, text_color, wine_color, battery_color;
   std::vector<unsigned> gpu_load_color;
   std::vector<unsigned> cpu_load_color;
   std::vector<unsigned> gpu_load_value;
   std::vector<unsigned> cpu_load_value;
   std::vector<unsigned> fps_color;
   std::vector<unsigned> fps_value;
   unsigned media_player_color;
   unsigned table_columns;
   bool no_small_font;
   float font_size, font_scale;
   float font_size_text;
   float font_scale_media_player;
   float background_alpha, alpha;
   float cellpadding_y;
   std::vector<KeySym> toggle_hud;
   std::vector<KeySym> toggle_fps_limit;
   std::vector<KeySym> toggle_logging;
   std::vector<KeySym> reload_cfg;
   std::vector<KeySym> upload_log;
   std::vector<KeySym> upload_logs;
   std::string time_format, output_folder, output_file;
   std::string pci_dev;
   std::string media_player_name;
   std::string cpu_text, gpu_text;
   std::vector<std::string> blacklist;
   unsigned log_interval, autostart_log;
   std::vector<std::string> media_player_format;
   std::vector<std::string> benchmark_percentiles;
   std::string font_file, font_file_text;
   uint32_t font_glyph_ranges;
   std::string custom_text_center;
   std::string custom_text;
   std::string background_image;
   std::string image;
   unsigned image_max_width;
   std::string config_file_path;
   std::unordered_map<std::string,std::string> options;
   int permit_upload;
   int fsr_steam_sharpness;
   unsigned short fcat_screen_edge;
   unsigned short fcat_overlay_width;

   size_t font_params_hash;
   size_t image_params_hash;
};

class overlay_params_wrapper {
public:
   overlay_params_wrapper(overlay_params& p, std::function<void(void)> f)
   : params(p), unlocker(f)
   {
   }

   ~overlay_params_wrapper()
   {
      unlocker();
   }

   overlay_params& params;
private:
   std::function<void(void)> unlocker;
};

class overlay_params_mutexed {
   overlay_params instance;
   std::mutex m;
   std::condition_variable reader_cv;
   std::condition_variable writer_cv;
   std::atomic_size_t waiting_writers, active_readers, active_writers;
public:
   overlay_params_mutexed() = default;

   overlay_params_mutexed(const overlay_params& r) {
      assign(r);
   }

   overlay_params_mutexed(const overlay_params&& r) {
      assign(r);
   }

   overlay_params_mutexed& operator=(const overlay_params& r)
   {
      assign(r);
      return *this;
   }

   void assign(const overlay_params& r)
   {
      std::unique_lock<std::mutex> lk(m);
      ++waiting_writers;
      writer_cv.wait(lk, [&](){ return active_readers == 0 && active_writers == 0; });
      ++active_writers;
      instance = r;
      --waiting_writers;
      --active_writers;
      reader_cv.notify_all();
   }

   const overlay_params* operator->() {
//       std::unique_lock<std::mutex> l(m);
      return &instance;
   }

   overlay_params_wrapper get()
   {
      std::unique_lock<std::mutex> lk(m);
      // If get() is called again from a sub function, it may block on waiting writers so just continue if active_readers > 0
      reader_cv.wait(lk, [&](){ return waiting_writers == 0 || active_readers > 0; });
      ++active_readers;
      return {instance, [&]() -> void {
         --active_readers;
         writer_cv.notify_all();
      }};
   }
};

extern overlay_params_mutexed g_overlay_params;

const extern char *overlay_param_names[];

void parse_overlay_config(struct overlay_params *params,
                       const char *env);

#ifdef __cplusplus
}
#endif

#endif /* MANGOHUD_OVERLAY_PARAMS_H */
