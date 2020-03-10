/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef OVERLAY_PARAMS_H
#define OVERLAY_PARAMS_H

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <X11/Xlib.h>

#define RGBGetBValue(rgb)   (rgb & 0x000000FF)
#define RGBGetGValue(rgb)   ((rgb >> 8) & 0x000000FF)
#define RGBGetRValue(rgb)   ((rgb >> 16) & 0x000000FF)

#define ToRGBColor(r, g, b, a) ((r << 16) | (g << 8) | (b));

#define OVERLAY_PARAMS                               \
   OVERLAY_PARAM_BOOL(fps)                           \
   OVERLAY_PARAM_BOOL(frame)                         \
   OVERLAY_PARAM_BOOL(frame_timing)                  \
   OVERLAY_PARAM_BOOL(present_timing)                \
   OVERLAY_PARAM_BOOL(gpu_timing)                    \
   OVERLAY_PARAM_BOOL(core_load)                     \
   OVERLAY_PARAM_BOOL(cpu_temp)                      \
   OVERLAY_PARAM_BOOL(gpu_temp)                      \
   OVERLAY_PARAM_BOOL(cpu_stats)                     \
   OVERLAY_PARAM_BOOL(gpu_stats)                     \
   OVERLAY_PARAM_BOOL(ram)                           \
   OVERLAY_PARAM_BOOL(vram)                          \
   OVERLAY_PARAM_BOOL(crosshair)                     \
   OVERLAY_PARAM_BOOL(time)                          \
   OVERLAY_PARAM_BOOL(full)                          \
   OVERLAY_PARAM_BOOL(read_cfg)                      \
   OVERLAY_PARAM_BOOL(io_read)                       \
   OVERLAY_PARAM_BOOL(io_write)                      \
   OVERLAY_PARAM_BOOL(gpu_mem_clock)                 \
   OVERLAY_PARAM_BOOL(gpu_core_clock)                \
   OVERLAY_PARAM_CUSTOM(fps_sampling_period)         \
   OVERLAY_PARAM_CUSTOM(output_file)                 \
   OVERLAY_PARAM_CUSTOM(position)                    \
   OVERLAY_PARAM_CUSTOM(width)                       \
   OVERLAY_PARAM_CUSTOM(height)                      \
   OVERLAY_PARAM_CUSTOM(no_display)                  \
   OVERLAY_PARAM_CUSTOM(control)                     \
   OVERLAY_PARAM_CUSTOM(fps_limit)                   \
   OVERLAY_PARAM_CUSTOM(vsync)                       \
   OVERLAY_PARAM_CUSTOM(font_size)                   \
   OVERLAY_PARAM_CUSTOM(toggle_hud)                  \
   OVERLAY_PARAM_CUSTOM(toggle_logging)              \
   OVERLAY_PARAM_CUSTOM(refresh_config)              \
   OVERLAY_PARAM_CUSTOM(crosshair_size)              \
   OVERLAY_PARAM_CUSTOM(offset_x)                    \
   OVERLAY_PARAM_CUSTOM(offset_y)                    \
   OVERLAY_PARAM_CUSTOM(crosshair_color)             \
   OVERLAY_PARAM_CUSTOM(background_alpha)            \
   OVERLAY_PARAM_CUSTOM(time_format)                 \
   OVERLAY_PARAM_CUSTOM(io_read)                     \
   OVERLAY_PARAM_CUSTOM(io_write)                    \
   OVERLAY_PARAM_CUSTOM(cpu_color)                   \
   OVERLAY_PARAM_CUSTOM(gpu_color)                   \
   OVERLAY_PARAM_CUSTOM(vram_color)                  \
   OVERLAY_PARAM_CUSTOM(ram_color)                   \
   OVERLAY_PARAM_CUSTOM(engine_color)                \
   OVERLAY_PARAM_CUSTOM(help)

enum overlay_param_position {
   LAYER_POSITION_TOP_LEFT,
   LAYER_POSITION_TOP_RIGHT,
   LAYER_POSITION_BOTTOM_LEFT,
   LAYER_POSITION_BOTTOM_RIGHT,
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
   FILE *output_file;
   int control;
   uint32_t fps_sampling_period; /* us */
   uint32_t fps_limit;
   uint32_t crosshair_size;
   bool help;
   bool no_display;
   bool full;
   bool io_read, io_write;
   unsigned width;
   unsigned height;
   unsigned offset_x;
   unsigned offset_y;
   unsigned vsync;
   unsigned crosshair_color, cpu_color, gpu_color, vram_color, ram_color, engine_color;
   unsigned tableCols;
   float font_size;
   float background_alpha;
   KeySym toggle_hud;
   KeySym toggle_logging;
   KeySym refresh_config;
   std::string time_format;
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
