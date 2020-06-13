#ifndef OVERLAY_PARAMS_H
#define OVERLAY_PARAMS_H

#include <string>
#include <vector>
#include <unordered_map>

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
   OVERLAY_PARAM_BOOL(gpu_temp)                      \
   OVERLAY_PARAM_BOOL(cpu_stats)                     \
   OVERLAY_PARAM_BOOL(gpu_stats)                     \
   OVERLAY_PARAM_BOOL(ram)                           \
   OVERLAY_PARAM_BOOL(vram)                          \
   OVERLAY_PARAM_BOOL(time)                          \
   OVERLAY_PARAM_BOOL(full)                          \
   OVERLAY_PARAM_BOOL(read_cfg)                      \
   OVERLAY_PARAM_BOOL(io_read)                       \
   OVERLAY_PARAM_BOOL(io_write)                      \
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
   OVERLAY_PARAM_CUSTOM(fps_sampling_period)         \
   OVERLAY_PARAM_CUSTOM(output_file)                 \
   OVERLAY_PARAM_CUSTOM(font_file)                   \
   OVERLAY_PARAM_CUSTOM(position)                    \
   OVERLAY_PARAM_CUSTOM(width)                       \
   OVERLAY_PARAM_CUSTOM(height)                      \
   OVERLAY_PARAM_CUSTOM(no_display)                  \
   OVERLAY_PARAM_CUSTOM(control)                     \
   OVERLAY_PARAM_CUSTOM(fps_limit)                   \
   OVERLAY_PARAM_CUSTOM(vsync)                       \
   OVERLAY_PARAM_CUSTOM(gl_vsync)                    \
   OVERLAY_PARAM_CUSTOM(font_size)                   \
   OVERLAY_PARAM_CUSTOM(font_scale_media_player)     \
   OVERLAY_PARAM_CUSTOM(toggle_hud)                  \
   OVERLAY_PARAM_CUSTOM(toggle_logging)              \
   OVERLAY_PARAM_CUSTOM(reload_cfg)                  \
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
   OVERLAY_PARAM_CUSTOM(alpha)                       \
   OVERLAY_PARAM_CUSTOM(log_duration)                \
   OVERLAY_PARAM_CUSTOM(pci_dev)                     \
   OVERLAY_PARAM_CUSTOM(media_player_name)           \
   OVERLAY_PARAM_CUSTOM(media_player_color)          \
   OVERLAY_PARAM_CUSTOM(media_player_order)          \
   OVERLAY_PARAM_CUSTOM(cpu_text)                    \
   OVERLAY_PARAM_CUSTOM(gpu_text)                    \
   OVERLAY_PARAM_CUSTOM(log_interval)                \
   OVERLAY_PARAM_CUSTOM(help)

enum overlay_param_position {
   LAYER_POSITION_TOP_LEFT,
   LAYER_POSITION_TOP_RIGHT,
   LAYER_POSITION_BOTTOM_LEFT,
   LAYER_POSITION_BOTTOM_RIGHT,
   LAYER_POSITION_TOP_CENTER,
};

enum overlay_plots {
    OVERLAY_PLOTS_frame_timing,
    OVERLAY_PLOTS_MAX,
};

enum media_player_order {
   MP_ORDER_TITLE,
   MP_ORDER_ARTIST,
   MP_ORDER_ALBUM,
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
   uint32_t fps_sampling_period; /* us */
   uint32_t fps_limit;
   bool help;
   bool no_display;
   bool full;
   bool io_read, io_write;
   unsigned width;
   unsigned height;
   int offset_x, offset_y;
   unsigned vsync;
   int gl_vsync;
   uint64_t log_duration;
   unsigned cpu_color, gpu_color, vram_color, ram_color, engine_color, io_color, frametime_color, background_color, text_color;
   unsigned media_player_color;
   unsigned tableCols;
   float font_size;
   float font_scale_media_player;
   float background_alpha, alpha;
   std::vector<KeySym> toggle_hud;
   std::vector<KeySym> toggle_logging;
   std::vector<KeySym> reload_cfg;
   std::string time_format, output_file, font_file;
   std::string pci_dev;
   std::string media_player_name;
   std::string cpu_text, gpu_text;
   unsigned log_interval;
   std::vector<media_player_order> media_player_order;

   std::string config_file_path;
   std::unordered_map<std::string,std::string> options;

};

const extern char *overlay_param_names[];

void parse_overlay_env(struct overlay_params *params,
                       const char *env);
void parse_overlay_config(struct overlay_params *params,
                       const char *env);

#ifdef __cplusplus
}
#endif

#endif /* OVERLAY_PARAMS_H */
