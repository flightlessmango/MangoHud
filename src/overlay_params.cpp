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
#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <array>
#include "imgui.h"
#include <iostream>

#include "overlay_params.h"
#include "overlay.h"
#include "config.h"

#include "mesa/util/os_socket.h"

#ifdef HAVE_X11
#include <X11/keysym.h>
#include "loaders/loader_x11.h"
#endif

#ifdef __gnu_linux__
#ifdef HAVE_DBUS
#include "dbus_info.h"
#endif
//#include <sys/sysinfo.h>
#include <wordexp.h>
#endif

static enum overlay_param_position

parse_position(const char *str)
{
   if (!str || !strcmp(str, "top-left"))
      return LAYER_POSITION_TOP_LEFT;
   if (!strcmp(str, "top-right"))
      return LAYER_POSITION_TOP_RIGHT;
   if (!strcmp(str, "bottom-left"))
      return LAYER_POSITION_BOTTOM_LEFT;
   if (!strcmp(str, "bottom-right"))
      return LAYER_POSITION_BOTTOM_RIGHT;
   return LAYER_POSITION_TOP_LEFT;
}

static int
parse_control(const char *str)
{
   int ret = os_socket_listen_abstract(str, 1);
   if (ret < 0) {
      fprintf(stderr, "ERROR: Couldn't create socket pipe at '%s'\n", str);
      fprintf(stderr, "ERROR: '%s'\n", strerror(errno));
      return ret;
   }

   os_socket_block(ret, false);

   return ret;
}

static float
parse_font_size(const char *str)
{
   return strtof(str, NULL);
}

static float
parse_background_alpha(const char *str)
{
   return strtof(str, NULL);
}

static float
parse_alpha(const char *str)
{
   return strtof(str, NULL);
}

#ifdef HAVE_X11
static KeySym
parse_toggle_hud(const char *str)
{
   if (g_x11->IsLoaded())
      return g_x11->XStringToKeysym(str);
   return 0;
}

static KeySym
parse_toggle_logging(const char *str)
{
   if (g_x11->IsLoaded())
      return g_x11->XStringToKeysym(str);
   return 0;
}

static KeySym
parse_reload_cfg(const char *str)
{
   if (g_x11->IsLoaded())
      return g_x11->XStringToKeysym(str);
   return 0;
}
#else
#define parse_toggle_hud(x)      0
#define parse_toggle_logging(x)  0
#define parse_reload_cfg(x)      0
#endif

static uint32_t
parse_fps_sampling_period(const char *str)
{
   return strtol(str, NULL, 0) * 1000;
}

static uint32_t
parse_fps_limit(const char *str)
{
   return strtol(str, NULL, 0);
}

static uint32_t
parse_crosshair_size(const char *str)
{
   return strtol(str, NULL, 0);
}

static bool
parse_no_display(const char *str)
{
   return strtol(str, NULL, 0) != 0;
}

static unsigned
parse_color(const char *str)
{
   return strtol(str, NULL, 16);
}

static unsigned
parse_unsigned(const char *str)
{
   return strtol(str, NULL, 0);
}

static signed
parse_signed(const char *str)
{
   return strtol(str, NULL, 0);
}

static std::string
parse_str(const char *str)
{
   return str;
}

static std::string
parse_path(const char *str)
{
#ifdef _XOPEN_SOURCE
   // Expand ~/ to home dir
   if (str[0] == '~') {
      std::string s;
      wordexp_t e;
      int ret;

      if (!(ret = wordexp(str, &e, 0)))
         s = e.we_wordv[0];
      wordfree(&e);

      if (!ret)
         return s;
   }
#endif
   return str;
}

#define parse_width(s) parse_unsigned(s)
#define parse_height(s) parse_unsigned(s)
#define parse_vsync(s) parse_unsigned(s)
#define parse_gl_vsync(s) parse_signed(s)
#define parse_offset_x(s) parse_unsigned(s)
#define parse_offset_y(s) parse_unsigned(s)
#define parse_log_duration(s) parse_unsigned(s)
#define parse_time_format(s) parse_str(s)
#define parse_output_file(s) parse_path(s)
#define parse_font_file(s) parse_path(s)
#define parse_io_read(s) parse_unsigned(s)
#define parse_io_write(s) parse_unsigned(s)

#define parse_crosshair_color(s) parse_color(s)
#define parse_cpu_color(s) parse_color(s)
#define parse_gpu_color(s) parse_color(s)
#define parse_vram_color(s) parse_color(s)
#define parse_ram_color(s) parse_color(s)
#define parse_engine_color(s) parse_color(s)
#define parse_io_color(s) parse_color(s)
#define parse_frametime_color(s) parse_color(s)
#define parse_background_color(s) parse_color(s)
#define parse_text_color(s) parse_color(s)

static bool
parse_help(const char *str)
{
   fprintf(stderr, "Layer params using VK_LAYER_MESA_OVERLAY_CONFIG=\n");
#define OVERLAY_PARAM_BOOL(name)                \
   fprintf(stderr, "\t%s=0|1\n", #name);
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
   fprintf(stderr, "\tposition=top-left|top-right|bottom-left|bottom-right\n");
   fprintf(stderr, "\tfps_sampling_period=number-of-milliseconds\n");
   fprintf(stderr, "\tno_display=0|1\n");
   fprintf(stderr, "\toutput_file=/path/to/output.txt\n");
   fprintf(stderr, "\twidth=width-in-pixels\n");
   fprintf(stderr, "\theight=height-in-pixels\n");

   return true;
}

static bool is_delimiter(char c)
{
   return c == 0 || c == ',' || c == ':' || c == ';' || c == '=';
}

static int
parse_string(const char *s, char *out_param, char *out_value)
{
   int i = 0;

   for (; !is_delimiter(*s); s++, out_param++, i++)
      *out_param = *s;

   *out_param = 0;

   if (*s == '=') {
      s++;
      i++;
      for (; !is_delimiter(*s); s++, out_value++, i++) {
         *out_value = *s;
         // Consume escaped delimiter, but don't escape null. Might be end of string.
         if (*s == '\\' && *(s + 1) != 0 && is_delimiter(*(s + 1))) {
            s++;
            i++;
            *out_value = *s;
         }
      }
   } else
      *(out_value++) = '1';
   *out_value = 0;

   if (*s && is_delimiter(*s)) {
      s++;
      i++;
   }

   if (*s && !i) {
      fprintf(stderr, "mesa-overlay: syntax error: unexpected '%c' (%i) while "
              "parsing a string\n", *s, *s);
      fflush(stderr);
   }

   return i;
}

const char *overlay_param_names[] = {
#define OVERLAY_PARAM_BOOL(name) #name,
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
};

void
parse_overlay_env(struct overlay_params *params,
                  const char *env)
{
   uint32_t num;
   char key[256], value[256];
   while ((num = parse_string(env, key, value)) != 0) {
      env += num;
      if (!strcmp("full", key)) {
         bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
#define OVERLAY_PARAM_BOOL(name) \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
         OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->enabled[OVERLAY_PARAM_ENABLED_crosshair] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_read_cfg] = read_cfg;
      }
#define OVERLAY_PARAM_BOOL(name)                                       \
      if (!strcmp(#name, key)) {                                       \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] =               \
            strtol(value, NULL, 0);                                    \
         continue;                                                     \
      }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
      if (!strcmp(#name, key)) {                                       \
         params->name = parse_##name(value);                           \
         continue;                                                     \
      }
      OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      fprintf(stderr, "Unknown option '%s'\n", key);
   }
}

void
parse_overlay_config(struct overlay_params *params,
                  const char *env)
{

   *params = {};

   /* Visible by default */
   params->enabled[OVERLAY_PARAM_ENABLED_fps] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_frame_timing] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_core_load] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_ram] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_vram] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_read_cfg] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_io_read] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_io_write] = false;
   params->fps_sampling_period = 500000; /* 500ms */
   params->width = 280;
   params->height = 140;
   params->control = -1;
   params->fps_limit = 0;
   params->vsync = -1;
   params->gl_vsync = -2;
   params->crosshair_size = 30;
   params->offset_x = 0;
   params->offset_y = 0;
   params->background_alpha = 0.5;
   params->alpha = 1.0;
   params->time_format = "%T";
   params->gpu_color = strtol("2e9762", NULL, 16);
   params->cpu_color = strtol("2e97cb", NULL, 16);
   params->vram_color = strtol("ad64c1", NULL, 16);
   params->ram_color = strtol("c26693", NULL, 16);
   params->engine_color = strtol("eb5b5b", NULL, 16);
   params->io_color = strtol("a491d3", NULL, 16);
   params->frametime_color = strtol("00ff00", NULL, 16);
   params->background_color = strtol("020202", NULL, 16);
   params->text_color = strtol("ffffff", NULL, 16);

#ifdef HAVE_X11
   params->toggle_hud = XK_F12;
   params->toggle_logging = XK_F2;
   params->reload_cfg = XK_F4;
#endif

#ifdef _WIN32
   params->toggle_hud = VK_F12;
   params->toggle_logging = VK_F2;
   params->reload_cfg = VK_F4;

   #undef parse_toggle_hud
   #undef parse_toggle_logging
   #undef parse_reload_cfg

   #define parse_toggle_hud(x)      params->toggle_hud
   #define parse_toggle_logging(x)  params->toggle_logging
   #define parse_reload_cfg(x)      params->reload_cfg
#endif

   // first pass with env var
   if (env)
      parse_overlay_env(params, env);

   bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
   if (!env || read_cfg) {

      // Get config options
      parseConfigFile(*params);
      if (params->options.find("full") != params->options.end() && params->options.find("full")->second != "0") {
#define OVERLAY_PARAM_BOOL(name) \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
            OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->enabled[OVERLAY_PARAM_ENABLED_crosshair] = 0;
         params->options.erase("full");
      }

      for (auto& it : params->options) {
#define OVERLAY_PARAM_BOOL(name)                                       \
         if (it.first == #name) {                                      \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] =            \
               strtol(it.second.c_str(), NULL, 0);                     \
            continue;                                                  \
         }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
         if (it.first == #name) {                                      \
            params->name = parse_##name(it.second.c_str());            \
            continue;                                                  \
         }
         OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         fprintf(stderr, "Unknown option '%s'\n", it.first.c_str());
      }

   }

   // second pass, override config file settings with MANGOHUD_CONFIG
   if (env && read_cfg)
      parse_overlay_env(params, env);

   // Convert from 0xRRGGBB to ImGui's format
   std::array<unsigned *, 10> colors = {
      &params->crosshair_color,
      &params->cpu_color,
      &params->gpu_color,
      &params->vram_color,
      &params->ram_color,
      &params->engine_color,
      &params->io_color,
      &params->background_color,
      &params->frametime_color,
      &params->text_color,
   };

   for (auto color : colors){
         *color =
         IM_COL32(RGBGetRValue(*color),
               RGBGetGValue(*color),
               RGBGetBValue(*color),
               255);
      }

   params->tableCols = 3;
   
   if (!params->font_size) {
      params->font_size = 24;
   } else {
      params->width = params->font_size * 11.7;
   }

   //increase hud width if io read and write
   if (params->enabled[OVERLAY_PARAM_ENABLED_io_read] && params->enabled[OVERLAY_PARAM_ENABLED_io_write] && params->width == 280)
      params->width = 15 * params->font_size;

   if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock] && params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp] && params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats]){
      params->tableCols = 4;
      params->width = 20 * params->font_size;
   }
   
   // set frametime limit
   if (params->fps_limit >= 0)
      fps_limit_stats.targetFrameTime = int64_t(1000000000.0 / params->fps_limit);

#ifdef HAVE_DBUS
   if (params->enabled[OVERLAY_PARAM_ENABLED_media_player]) {
      try {
         dbusmgr::dbus_mgr.init();
         get_spotify_metadata(dbusmgr::dbus_mgr, spotify);
      } catch (std::runtime_error& e) {
         std::cerr << "Failed to get initial Spotify metadata: " << e.what() << std::endl;
      }
   } else {
      dbusmgr::dbus_mgr.deinit();
      spotify.valid = false;
   }
#endif

}
