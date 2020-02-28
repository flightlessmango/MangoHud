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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <errno.h>
#include <sys/sysinfo.h>
#include <X11/Xlib.h>
#include "X11/keysym.h"

#include "overlay_params.h"
#include "config.h"

#include "mesa/util/os_socket.h"

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

static FILE *
parse_output_file(const char *str)
{
   return fopen(str, "w+");
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

static KeySym
parse_toggle_hud(const char *str)
{
   return XStringToKeysym(str);
}

static KeySym
parse_toggle_logging(const char *str)
{
   return XStringToKeysym(str);
}

static KeySym
parse_refresh_config(const char *str)
{
   return XStringToKeysym(str);
}

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

#define parse_width(s) parse_unsigned(s)
#define parse_height(s) parse_unsigned(s)
#define parse_vsync(s) parse_unsigned(s)
#define parse_offset_x(s) parse_unsigned(s)
#define parse_offset_y(s) parse_unsigned(s)

#define parse_crosshair_color(s) parse_color(s)

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
      for (; !is_delimiter(*s); s++, out_value++, i++)
         *out_value = *s;
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

   memset(params, 0, sizeof(*params));

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
   params->fps_sampling_period = 500000; /* 500ms */
   params->width = 280;
   params->height = 140;
   params->control = -1;
   params->toggle_hud = XK_F12;
   params->toggle_logging = XK_F2;
   params->refresh_config = XK_F4;
   params->fps_limit = 0;
   params->vsync = -1;
   params->crosshair_size = 30;
   params->offset_x = 0;
   params->offset_y = 0;
   params->background_alpha = 0.5;

   // first pass with env var
   if (env)
      parse_overlay_env(params, env);

   bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
   if (!env || read_cfg) {

      // Get config options
      parseConfigFile();
      if (options.find("full") != options.end() && options.find("full")->second != "0") {
#define OVERLAY_PARAM_BOOL(name) \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
            OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->enabled[OVERLAY_PARAM_ENABLED_crosshair] = 0;
         options.erase("full");
      }

      for (auto& it : options) {
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

   // Command buffer gets reused and timestamps cause hangs for some reason, force off for now
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_timing] = false;
}