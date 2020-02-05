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
#include <string.h>
#include <errno.h>
#include <sys/sysinfo.h>

#include "overlay_params.h"

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

static uint32_t
parse_fps_sampling_period(const char *str)
{
   return strtol(str, NULL, 0) * 1000;
}

static bool
parse_no_display(const char *str)
{
   return strtol(str, NULL, 0) != 0;
}

static unsigned
parse_unsigned(const char *str)
{
   return strtol(str, NULL, 0);
}

#define parse_width(s) parse_unsigned(s)
#define parse_height(s) parse_unsigned(s)

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
   params->fps_sampling_period = 500000; /* 500ms */
   params->width = 280;
   params->height = 140;
   params->control = -1;

   if (!env)
      return;

   while ((num = parse_string(env, key, value)) != 0) {
      env += num;

#define OVERLAY_PARAM_BOOL(name)                                        \
      if (!strcmp(#name, key)) {                                        \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] =                \
            strtol(value, NULL, 0);                                     \
         continue;                                                      \
      }
#define OVERLAY_PARAM_CUSTOM(name)               \
      if (!strcmp(#name, key)) {                 \
         params->name = parse_##name(value);     \
         continue;                               \
      }
      OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      fprintf(stderr, "Unknown option '%s'\n", key);
   }
   // if font_size is used and height has not been changed from default
   // increase height as needed based on font_size
   bool heightChanged = false;
   
   if (params->height != 140)
      heightChanged = true;

   int FrameTimeGraphHeight = 0;
   if (params->enabled[OVERLAY_PARAM_ENABLED_frame_timing])
    FrameTimeGraphHeight = 50;

   if (!params->font_size)
      params->font_size = 24.0f;

   if (params->font_size && !heightChanged)
      params->height = (params->font_size - 3 * 2) * 3 + FrameTimeGraphHeight;

   // Apply more hud height if cores are enabled
   if (params->enabled[OVERLAY_PARAM_ENABLED_core_load] && !heightChanged)
     params->height += ((params->font_size - 3) * get_nprocs());

   if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats])
      params->height += (params->font_size - 3);

   if (params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats])
      params->height += (params->font_size - 3);

   if (params->enabled[OVERLAY_PARAM_ENABLED_ram])
      params->height += (params->font_size - 3);

   if (params->enabled[OVERLAY_PARAM_ENABLED_vram])
      params->height += (params->font_size - 3);
}     
