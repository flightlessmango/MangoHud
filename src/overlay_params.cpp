#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef __linux__
#include <wordexp.h>
#include <unistd.h>
#endif
#include "imgui.h"
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <array>
#include <functional>
#include <spdlog/spdlog.h>

#include "overlay_params.h"
#include "overlay.h"
#include "config.h"
#include "string_utils.h"
#include "hud_elements.h"
#include "blacklist.h"
#include "mesa/util/os_socket.h"
#include "file_utils.h"

#ifdef HAVE_X11
#include <X11/keysym.h>
#include "loaders/loader_x11.h"
#endif

#include "dbus_info.h"

#include "app/mangoapp.h"
#include "fps_metrics.h"

std::unique_ptr<fpsMetrics> fpsmetrics;

#if __cplusplus >= 201703L

template<typename... Ts>
size_t get_hash(Ts const&... args)
{
   size_t hash = 0;
   ( (hash ^= std::hash<Ts>{}(args) << 1), ...);
   return hash;
}

#else

// C++17 has `if constexpr` so this won't be needed then
template<typename... Ts>
size_t get_hash()
{
   return 0;
}

template<typename T, typename... Ts>
size_t get_hash(T const& first, Ts const&... rest)
{
   size_t hash = std::hash<T>{}(first);
#if __cplusplus >= 201703L
   if constexpr (sizeof...(rest) > 0)
#endif
      hash ^= get_hash(rest...) << 1;

   return hash;
}

#endif

static enum overlay_param_position
parse_position(const char *str)
{
   if (!str || !strcmp(str, "top-left"))
      return LAYER_POSITION_TOP_LEFT;
   if (!strcmp(str, "top-right"))
      return LAYER_POSITION_TOP_RIGHT;
   if (!strcmp(str, "middle-left"))
      return LAYER_POSITION_MIDDLE_LEFT;
   if (!strcmp(str, "middle-right"))
      return LAYER_POSITION_MIDDLE_RIGHT;
   if (!strcmp(str, "bottom-left"))
      return LAYER_POSITION_BOTTOM_LEFT;
   if (!strcmp(str, "bottom-right"))
      return LAYER_POSITION_BOTTOM_RIGHT;
   if (!strcmp(str, "top-center"))
      return LAYER_POSITION_TOP_CENTER;
   if (!strcmp(str, "bottom-center"))
      return LAYER_POSITION_BOTTOM_CENTER;
   return LAYER_POSITION_TOP_LEFT;
}

static int
parse_control(const char *str)
{
   std::string path(str);
   size_t npos = path.find("%p");
   if (npos != std::string::npos)
      path.replace(npos, 2, std::to_string(getpid()));
   SPDLOG_DEBUG("Socket: {}", path);

   int ret = os_socket_listen_abstract(path.c_str(), 1);
   if (ret < 0) {
      SPDLOG_ERROR("Couldn't create socket pipe at '{}'", path);
      SPDLOG_ERROR("ERROR: '{}'", strerror(errno));
      return ret;
   }

   os_socket_block(ret, false);

   return ret;
}

static float
parse_float(const char *str)
{
   float val = 0;
   std::stringstream ss(str);
   ss.imbue(std::locale::classic());
   ss >> val;
   return val;
}

#ifdef HAVE_X11
static std::vector<KeySym>
parse_string_to_keysym_vec(const char *str)
{
   std::vector<KeySym> keys;
   if(get_libx11()->IsLoaded())
   {
      auto keyStrings = str_tokenize(str);
      for (auto& ks : keyStrings) {
         trim(ks);
         KeySym xk = get_libx11()->XStringToKeysym(ks.c_str());
         if (xk)
            keys.push_back(xk);
         else
            SPDLOG_ERROR("Unrecognized key: '{}'", ks);
      }
   }
   return keys;
}

#define parse_toggle_hud            parse_string_to_keysym_vec
#define parse_toggle_hud_position   parse_string_to_keysym_vec
#define parse_toggle_logging        parse_string_to_keysym_vec
#define parse_reload_cfg            parse_string_to_keysym_vec
#define parse_upload_log            parse_string_to_keysym_vec
#define parse_upload_logs           parse_string_to_keysym_vec
#define parse_toggle_fps_limit      parse_string_to_keysym_vec
#define parse_toggle_preset         parse_string_to_keysym_vec
#define parse_reset_fps_metrics     parse_string_to_keysym_vec

#else
#define parse_toggle_hud(x)            {}
#define parse_toggle_hud_position(x)   {}
#define parse_toggle_logging(x)        {}
#define parse_reload_cfg(x)            {}
#define parse_upload_log(x)            {}
#define parse_upload_logs(x)           {}
#define parse_toggle_fps_limit(x)      {}
#define parse_toggle_preset(x)         {}
#define parse_reset_fps_metrics(x)     {}
#endif

// NOTE: This is NOT defined as an OVERLAY_PARAM and will be called manually
static std::vector<int>
parse_preset(const char *str)
{
  std::vector<int> presets;
  auto preset_strings = str_tokenize(str);

  for (auto& value : preset_strings) {
    trim(value);

    uint32_t as_int;
    try {
      as_int = static_cast<int>(std::stoi(value));
    } catch (const std::invalid_argument&) {
      SPDLOG_ERROR("invalid preset value: '{}'", value);
      continue;
    }

    presets.push_back(as_int);
  }

  return presets;
}

static uint32_t
parse_fps_sampling_period(const char *str)
{
   return strtol(str, NULL, 0) * 1000000; /* ms to ns */
}

static std::vector<std::uint32_t>
parse_fps_limit(const char *str)
{
   std::vector<std::uint32_t> fps_limit;
   auto fps_limit_strings = str_tokenize(str);

   for (auto& value : fps_limit_strings) {
      trim(value);

      uint32_t as_int;
      try {
         as_int = static_cast<uint32_t>(std::stoul(value));
      } catch (const std::invalid_argument&) {
         SPDLOG_ERROR("invalid fps_limit value: '{}'", value);
         continue;
      }

      fps_limit.push_back(as_int);
   }

   return fps_limit;
}

static enum fps_limit_method
parse_fps_limit_method(const char *str)
{
   if (!strcmp(str, "early")) {
      return FPS_LIMIT_METHOD_EARLY;
   }

   return FPS_LIMIT_METHOD_LATE;
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

static std::vector<unsigned>
parse_load_color(const char *str)
{
   std::vector<unsigned> load_colors;
   auto tokens = str_tokenize(str);
   std::string token;
   for (auto& token : tokens) {
      trim(token);
      load_colors.push_back(std::stoi(token, NULL, 16));
   }
   while (load_colors.size() != 3) {
      load_colors.push_back(std::stoi("FFFFFF" , NULL, 16));
   }

    return load_colors;
}

static std::vector<unsigned>
parse_load_value(const char *str)
{
   std::vector<unsigned> load_value;
   auto tokens = str_tokenize(str);
   std::string token;
   for (auto& token : tokens) {
      trim(token);
      load_value.push_back(std::stoi(token));
   }
    return load_value;
}


static std::vector<std::string>
parse_str_tokenize(const char *str, const std::string& delims = ",:+", bool btrim = true)
{
   std::vector<std::string> data;
   auto tokens = str_tokenize(str, delims);
   std::string token;
   for (auto& token : tokens) {
      if (btrim)
         trim(token);
      data.push_back(token);
   }
    return data;
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
      std::stringstream s;
      wordexp_t e;
      int ret;

      if (!(ret = wordexp(str, &e, 0))) {
         for(size_t i = 0; i < e.we_wordc; i++)
         {
            if (i > 0)
               s << " ";
            s << e.we_wordv[i];
         }
      }
      wordfree(&e);

      if (!ret)
         return s.str();
   }
#endif
   return str;
}

static std::vector<std::string>
parse_benchmark_percentiles(const char *str)
{
   std::vector<std::string> percentiles;
   auto tokens = str_tokenize(str);
   for (auto& value : tokens) {
      trim(value);

      if (value == "AVG") {
         percentiles.push_back(value);
         continue;
      }

      float as_float;
      size_t float_len = 0;

      try {
         as_float = parse_float(value, &float_len);
      } catch (const std::invalid_argument&) {
         SPDLOG_ERROR("invalid benchmark percentile: '{}'", value);
         continue;
      }

      if (float_len != value.length()) {
         SPDLOG_ERROR("invalid benchmark percentile: '{}'", value);
         continue;
      }

      if (as_float > 100 || as_float < 0) {
         SPDLOG_ERROR("benchmark percentile is not between 0 and 100 ({})", value);
         continue;
      }

      percentiles.push_back(value);
   }

   return percentiles;
}

static uint32_t
parse_font_glyph_ranges(const char *str)
{
   uint32_t fg = 0;
   auto tokens = str_tokenize(str);
   for (auto& token : tokens) {
      trim(token);
      std::transform(token.begin(), token.end(), token.begin(), ::tolower);

      if (token == "korean")
         fg |= FG_KOREAN;
      else if (token == "chinese")
         fg |= FG_CHINESE_FULL;
      else if (token == "chinese_simplified")
         fg |= FG_CHINESE_SIMPLIFIED;
      else if (token == "japanese")
         fg |= FG_JAPANESE;
      else if (token == "cyrillic")
         fg |= FG_CYRILLIC;
      else if (token == "thai")
         fg |= FG_THAI;
      else if (token == "vietnamese")
         fg |= FG_VIETNAMESE;
      else if (token == "latin_ext_a")
         fg |= FG_LATIN_EXT_A;
      else if (token == "latin_ext_b")
         fg |= FG_LATIN_EXT_B;
   }
   return fg;
}

static gl_size_query
parse_gl_size_query(const char *str)
{
   std::string value(str);
   trim(value);
   std::transform(value.begin(), value.end(), value.begin(), ::tolower);
   if (value == "viewport")
      return GL_SIZE_VIEWPORT;
   if (value == "scissorbox")
      return GL_SIZE_SCISSORBOX;
   return GL_SIZE_DRAWABLE;
}

static std::vector<std::string>
parse_fps_metrics(const char *str){
   std::vector<std::string> metrics;
   auto tokens = str_tokenize(str);
   for (auto& token : tokens) {
      metrics.push_back(token);
   }

   fpsmetrics.release();
   fpsmetrics = std::make_unique<fpsMetrics>(metrics);

   return metrics;
}

#define parse_width(s) parse_unsigned(s)
#define parse_height(s) parse_unsigned(s)
#define parse_vsync(s) parse_unsigned(s)
#define parse_gl_vsync(s) parse_signed(s)
#define parse_offset_x(s) parse_unsigned(s)
#define parse_offset_y(s) parse_unsigned(s)
#define parse_log_duration(s) parse_unsigned(s)
#define parse_time_format(s) parse_str(s)
#define parse_output_folder(s) parse_path(s)
#define parse_output_file(s) parse_path(s)
#define parse_font_file(s) parse_path(s)
#define parse_font_file_text(s) parse_path(s)
#define parse_io_read(s) parse_unsigned(s)
#define parse_io_write(s) parse_unsigned(s)
#define parse_pci_dev(s) parse_str(s)
#define parse_media_player_name(s) parse_str(s)
#define parse_font_scale_media_player(s) parse_float(s)
#define parse_cpu_text(s) parse_str(s)
#define parse_gpu_text(s) parse_str(s)
#define parse_fps_text(s) parse_str(s)
#define parse_log_interval(s) parse_unsigned(s)
#define parse_font_size(s) parse_float(s)
#define parse_font_size_text(s) parse_float(s)
#define parse_font_scale(s) parse_float(s)
#define parse_background_alpha(s) parse_float(s)
#define parse_alpha(s) parse_float(s)
#define parse_permit_upload(s) parse_unsigned(s)
#define parse_no_small_font(s) parse_unsigned(s) != 0
#define parse_cellpadding_y(s) parse_float(s)
#define parse_table_columns(s) parse_unsigned(s)
#define parse_autostart_log(s) parse_unsigned(s)
#define parse_gl_bind_framebuffer(s) parse_unsigned(s)
#define parse_gl_dont_flip(s) parse_unsigned(s) != 0
#define parse_round_corners(s) parse_unsigned(s)
#define parse_fcat_overlay_width(s) parse_unsigned(s)
#define parse_fcat_screen_edge(s) parse_unsigned(s)
#define parse_picmip(s) parse_signed(s)
#define parse_af(s) parse_signed(s)

#define parse_cpu_color(s) parse_color(s)
#define parse_gpu_color(s) parse_color(s)
#define parse_vram_color(s) parse_color(s)
#define parse_ram_color(s) parse_color(s)
#define parse_engine_color(s) parse_color(s)
#define parse_io_color(s) parse_color(s)
#define parse_frametime_color(s) parse_color(s)
#define parse_background_color(s) parse_color(s)
#define parse_text_color(s) parse_color(s)
#define parse_media_player_color(s) parse_color(s)
#define parse_wine_color(s) parse_color(s)
#define parse_gpu_load_color(s) parse_load_color(s)
#define parse_cpu_load_color(s) parse_load_color(s)
#define parse_gpu_load_value(s) parse_load_value(s)
#define parse_cpu_load_value(s) parse_load_value(s)
#define parse_blacklist(s) parse_str_tokenize(s)
#define parse_custom_text_center(s) parse_str(s)
#define parse_custom_text(s) parse_str(s)
#define parse_fps_value(s) parse_load_value(s)
#define parse_fps_color(s) parse_load_color(s)
#define parse_battery_color(s) parse_color(s)
#define parse_media_player_format(s) parse_str_tokenize(s, ";", false)
#define parse_fsr_steam_sharpness(s) parse_float(s)
#define parse_text_outline_color(s) parse_color(s)
#define parse_text_outline_thickness(s) parse_float(s)
#define parse_device_battery(s) parse_str_tokenize(s)

static bool
parse_help(const char *str)
{
   fprintf(stderr, "Layer params using MANGOHUD_CONFIG=\n");
#define OVERLAY_PARAM_BOOL(name)                \
   fprintf(stderr, "\t%s=0|1\n", #name);
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
   fprintf(stderr, "\tposition=top-left|top-right|bottom-left|bottom-right\n");
   fprintf(stderr, "\tfps_sampling_period=number-of-milliseconds\n");
   fprintf(stderr, "\tno_display=0|1\n");
   fprintf(stderr, "\toutput_folder=/path/to/folder\n");
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
      SPDLOG_ERROR("syntax error: unexpected '{0:c}' ({0:d}) while "
              "parsing a string", *s);
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

static void
parse_overlay_env(struct overlay_params *params,
                  const char *env)
{
   uint32_t num;
   char key[256], value[256];
   while ((num = parse_string(env, key, value)) != 0) {
      trim_char(key);
      trim_char(value);
      env += num;
      if (!strcmp("full", key)) {
         bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
#define OVERLAY_PARAM_BOOL(name) \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
         OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->enabled[OVERLAY_PARAM_ENABLED_histogram] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_gpu_load_change] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_cpu_load_change] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_fps_only] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_fps_color_change] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_core_load_change] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_battery_icon] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_mangoapp_steam] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_hide_fsr_sharpness] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_throttling_status] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_read_cfg] = read_cfg;
         params->enabled[OVERLAY_PARAM_ENABLED_fcat] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_horizontal] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_horizontal_stretch] = 1;
         params->enabled[OVERLAY_PARAM_ENABLED_hud_no_margin] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_log_versioning] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_exec_name] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_trilinear] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_bicubic] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_retro] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_debug] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_engine_short_names] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_dynamic_frame_timing] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_core_bars] = false;
      }
#define OVERLAY_PARAM_BOOL(name)                                       \
      if (!strcmp(#name, key)) {                                       \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] =               \
            strtol(value, NULL, 0);                                    \
         add_to_options(params, key, value);                           \
         continue;                                                     \
      }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
      if (!strcmp(#name, key)) {                                       \
         params->name = parse_##name(value);                           \
         add_to_options(params, key, value);                           \
         continue;                                                     \
      }
      OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      SPDLOG_ERROR("Unknown option '{}'", key);
   }
}

static void set_param_defaults(struct overlay_params *params){
   params->enabled[OVERLAY_PARAM_ENABLED_fps] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_frame_timing] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_core_load] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_core_bars] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_power] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_junction_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_temp] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_ram] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_swap] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_vram] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_read_cfg] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_io_read] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_io_write] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_wine] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_load_change] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_cpu_load_change] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_core_load_change] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_gpu_voltage] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_frametime] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_fps_only] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_device_battery_icon] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_throttling_status] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_fcat] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_horizontal_stretch] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_engine_short_names] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_text_outline] = true;
   params->enabled[OVERLAY_PARAM_ENABLED_dynamic_frame_timing] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_duration] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_frame_timing_detailed] = false;
   params->fps_sampling_period = 500000000; /* 500ms */
   params->width = 0;
   params->height = 140;
   params->control = -1;
   params->fps_limit = { 0 };
   params->fps_limit_method = FPS_LIMIT_METHOD_LATE;
   params->vsync = -1;
   params->gl_vsync = -2;
   params->offset_x = 0;
   params->offset_y = 0;
   params->background_alpha = 0.5;
   params->alpha = 1.0;
   params->fcat_screen_edge = 0;
   params->fcat_overlay_width = 24;
   params->time_format = "%T";
   params->gpu_color = 0x2e9762;
   params->cpu_color = 0x2e97cb;
   params->vram_color = 0xad64c1;
   params->ram_color = 0xc26693;
   params->engine_color = 0xeb5b5b;
   params->io_color = 0xa491d3;
   params->frametime_color = 0x00ff00;
   params->background_color = 0x020202;
   params->text_color = 0xffffff;
   params->media_player_color = 0xffffff;
   params->media_player_name = "";
   params->font_scale = 1.0f;
   params->wine_color = 0xeb5b5b;
   params->gpu_load_color = { 0x39f900, 0xfdfd09, 0xb22222 };
   params->cpu_load_color = { 0x39f900, 0xfdfd09, 0xb22222 };
   params->font_scale_media_player = 0.55f;
   params->log_interval = 0;
   params->media_player_format = { "{title}", "{artist}", "{album}" };
   params->permit_upload = 0;
   params->benchmark_percentiles = { "97", "AVG"};
   params->gpu_load_value = { 60, 90 };
   params->cpu_load_value = { 60, 90 };
   params->cellpadding_y = -0.085;
   params->fps_color = { 0xb22222, 0xfdfd09, 0x39f900 };
   params->fps_value = { 30, 60 };
   params->round_corners = 0;
   params->battery_color =0xff9078;
   params->fsr_steam_sharpness = -1;
   params->picmip = -17;
   params->af = -1;
   params->font_size = 24;
   params->table_columns = 3;
   params->text_outline_color = 0x000000;
   params->text_outline_thickness = 1.5;
}

void
parse_overlay_config(struct overlay_params *params,
                  const char *env, bool use_existing_preset)
{
   std::vector<int> default_preset = {-1, 0, 1, 2, 3, 4};
   *params = {
     .preset = use_existing_preset ? params->preset : default_preset
   };
   set_param_defaults(params);

#ifdef HAVE_X11
   params->toggle_hud = { XK_Shift_R, XK_F12 };
   params->toggle_hud_position = { XK_Shift_R, XK_F11 };
   params->toggle_preset = { XK_Shift_R, XK_F10 };
   params->reset_fps_metrics = { XK_Shift_R, XK_F9};
   params->toggle_fps_limit = { XK_Shift_L, XK_F1 };
   params->toggle_logging = { XK_Shift_L, XK_F2 };
   params->reload_cfg = { XK_Shift_L, XK_F4 };
   params->upload_log = { XK_Shift_L, XK_F3 };
   params->upload_logs = { XK_Control_L, XK_F3 };
#endif

#ifdef _WIN32
   params->toggle_hud = { VK_F12 };
   params->toggle_preset = { VK_F10 };
   params->reset_fps_metrics = { VK_F9};
   params->toggle_fps_limit = { VK_F3 };
   params->toggle_logging = { VK_F2 };
   params->reload_cfg = { VK_F4 };

   #undef parse_toggle_hud
   #undef parse_toggle_fps_limit
   #undef parse_toggle_preset
   #undef parse_toggle_logging
   #undef parse_reload_cfg

   #define parse_toggle_hud(x)         params->toggle_hud
   #define parse_toggle_preset(x)      params->toggle_preset
   #define parse_toggle_fps_limit(x)   params->toggle_fps_limit
   #define parse_toggle_logging(x)     params->toggle_logging
   #define parse_reload_cfg(x)         params->reload_cfg
#endif

   HUDElements.ordered_functions.clear();
   HUDElements.exec_list.clear();
   // first pass with env var
   if (env)
      parse_overlay_env(params, env);

   bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
   if (!env || read_cfg) {

      // Get config options
      parseConfigFile(*params);

      if (!use_existing_preset) {
         if (params->options.find("preset") != params->options.end()) {
            auto presets = parse_preset(params->options.find("preset")->second.c_str());
            if (!presets.empty())
               params->preset = presets;
         }
        current_preset = params->preset[0];
      }

      presets(current_preset, params);

      if (params->options.find("full") != params->options.end() && params->options.find("full")->second != "0") {
#define OVERLAY_PARAM_BOOL(name) \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
            OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->enabled[OVERLAY_PARAM_ENABLED_histogram] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_fps_only] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_battery_icon] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_mangoapp_steam] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_hide_fsr_sharpness] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_throttling_status] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_fcat] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_horizontal] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_hud_no_margin] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_log_versioning] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_exec_name] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_trilinear] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_bicubic] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_retro] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_debug] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_engine_short_names] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_dynamic_frame_timing] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit] = 0;
         params->enabled[OVERLAY_PARAM_ENABLED_duration] = false;
         params->enabled[OVERLAY_PARAM_ENABLED_core_bars] = false;
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
         if (it.first == "preset") {
            continue;
         }
         SPDLOG_ERROR("Unknown option '{}'", it.first.c_str());
      }
   }

   // TODO decide what to do for legacy_layout=0
   // second pass, override config file settings with MANGOHUD_CONFIG
   if (params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout] && env && read_cfg) {
      // If passing legacy_layout=0 to MANGOHUD_CONFIG anyway then clear first pass' results
      HUDElements.ordered_functions.clear();
      parse_overlay_env(params, env);
   }

   // If fps_only param is enabled disable legacy_layout
   if (params->enabled[OVERLAY_PARAM_ENABLED_fps_only])
      params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout] = false;

   if (is_blacklisted())
      return;

   if (params->font_scale_media_player <= 0.f)
      params->font_scale_media_player = 0.55f;

   // Convert from 0xRRGGBB to ImGui's format
   std::array<unsigned *, 22> colors = {
      &params->cpu_color,
      &params->gpu_color,
      &params->vram_color,
      &params->ram_color,
      &params->engine_color,
      &params->io_color,
      &params->background_color,
      &params->frametime_color,
      &params->text_color,
      &params->media_player_color,
      &params->wine_color,
      &params->battery_color,
      &params->gpu_load_color[0],
      &params->gpu_load_color[1],
      &params->gpu_load_color[2],
      &params->cpu_load_color[0],
      &params->cpu_load_color[1],
      &params->cpu_load_color[2],
      &params->fps_color[0],
      &params->fps_color[1],
      &params->fps_color[2],
      &params->text_outline_color,
   };

   for (auto color : colors){
      *color =
      IM_COL32(RGBGetRValue(*color),
               RGBGetGValue(*color),
               RGBGetBValue(*color),
               255);
   }

   params->table_columns = std::max(1u, std::min(64u, params->table_columns));

   //increase hud width if io read and write
   if (!params->width && !params->enabled[OVERLAY_PARAM_ENABLED_horizontal]) {
      params->width = params->font_size * params->font_scale * params->table_columns * 4.6;

      if ((params->enabled[OVERLAY_PARAM_ENABLED_io_read] || params->enabled[OVERLAY_PARAM_ENABLED_io_write])) {
         params->width += 2 * params->font_size * params->font_scale;
      }

      // Treat it like hud would need to be ~7 characters wider with default font.
      if (params->no_small_font)
         params->width += 7 * params->font_size * params->font_scale;
   }

   params->font_params_hash = get_hash(params->font_size,
                                 params->font_size_text,
                                 params->no_small_font,
                                 params->font_file,
                                 params->font_file_text,
                                 params->font_glyph_ranges
                                );

   // set frametime limit
   using namespace std::chrono;
   if (params->fps_limit.size() > 0 && params->fps_limit[0] > 0)
      fps_limit_stats.targetFrameTime = duration_cast<Clock::duration>(duration<double>(1) / params->fps_limit[0]);
   else
      fps_limit_stats.targetFrameTime = {};

   fps_limit_stats.method = params->fps_limit_method;

#ifdef HAVE_DBUS
   if (params->enabled[OVERLAY_PARAM_ENABLED_media_player]) {
      if (dbusmgr::dbus_mgr.init(dbusmgr::SRV_MPRIS))
         dbusmgr::dbus_mgr.init_mpris(params->media_player_name);
   } else {
      dbusmgr::dbus_mgr.deinit(dbusmgr::SRV_MPRIS);
      main_metadata.meta.valid = false;
   }

   // if (params->enabled[OVERLAY_PARAM_ENABLED_gamemode])
   // {
   //    if (dbusmgr::dbus_mgr.init(dbusmgr::SRV_GAMEMODE))
   //       HUDElements.gamemode_bol = dbusmgr::dbus_mgr.gamemode_enabled(getpid());
   // }
   // else
   //    dbusmgr::dbus_mgr.deinit(dbusmgr::SRV_GAMEMODE);

#endif

   if(!params->output_file.empty()) {
      SPDLOG_INFO("output_file is deprecated, use output_folder instead");
   }

   auto real_size = params->font_size * params->font_scale;
   real_font_size = ImVec2(real_size, real_size / 2);
   HUDElements.params = params;

   for (const auto& option : HUDElements.options) {
      SPDLOG_DEBUG("Param: '{}' = '{}'", option.first, option.second);
   }

   if (params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout]) {
      HUDElements.legacy_elements();
   } else {
      for (auto& option : HUDElements.options) {
         HUDElements.sort_elements(option);
      }
   }

   // Needs ImGui context but it is null here for OpenGL so just note it and update somewhere else
   HUDElements.colors.update = true;
   if (params->no_small_font)
      HUDElements.text_column = 2;
   else
      HUDElements.text_column = 1;

   if(logger && logger->is_active()){
      SPDLOG_DEBUG("Stopped logging because config reloaded");
      logger->stop_logging();
   }
   logger = std::make_unique<Logger>(params);
#ifdef MANGOAPP
   {
      extern bool new_frame;
      std::lock_guard<std::mutex> lk(mangoapp_m);
      params->no_display = params->no_display;
      new_frame = true; // we probably changed how we look.
   }
   mangoapp_cv.notify_one();
   g_fsrSharpness = params->fsr_steam_sharpness;
#endif
}

bool parse_preset_config(int preset, struct overlay_params *params){
   const char *presets_file_env = getenv("MANGOHUD_PRESETSFILE");
   const std::string config_dir = get_config_dir();
   std::string preset_path = presets_file_env ? presets_file_env : config_dir + "/MangoHud/" + "presets.conf";

   char preset_string[20];
   snprintf(preset_string, sizeof(preset_string), "[preset %d]", preset);

   std::ifstream stream(preset_path);
   stream.imbue(std::locale::classic());

   if (!stream.good()) {
      SPDLOG_ERROR("Failed to read presets file: '{}'", preset_path);
      return false;
   }

   std::string line;
   bool found_preset = false;

   while (std::getline(stream, line)) {
      trim(line);

      if (line == "")
         continue;

      if (line == preset_string) {
         found_preset = true;
         continue;
      }

      if (found_preset) {
         if (line.front() == '[' && line.back() == ']')
            break;

         if (line == "inherit")
            presets(preset, params, true);

         parseConfigLine(line, params->options);
      }
   }

   return found_preset;
}

void add_to_options(struct overlay_params *params, std::string option, std::string value){
   HUDElements.options.push_back({option, value});
   params->options[option] = value;
}

int i = 0;
void presets(int preset, struct overlay_params *params, bool inherit) {
   if (!inherit && parse_preset_config(preset, params))
         return;

   switch(preset) {
      case 0:
         params->no_display = 1;
         break;

      case 1:
         params->width = 40;
         add_to_options(params, "legacy_layout", "0");
         add_to_options(params, "cpu_stats", "0");
         add_to_options(params, "gpu_stats", "0");
         add_to_options(params, "fps", "1");
         add_to_options(params, "fps_only", "1");
         add_to_options(params, "frametime", "0");
         add_to_options(params, "debug", "0");
         break;

      case 2:
         params->table_columns = 20;
         add_to_options(params, "horizontal", "1");
         add_to_options(params, "legacy_layout", "0");
         add_to_options(params, "fps", "1");
         add_to_options(params, "table_columns", "20");
         add_to_options(params, "frame_timing", "1");
         add_to_options(params, "frametime", "0");
         add_to_options(params, "cpu_stats", "1");
         add_to_options(params, "gpu_stats", "1");
         add_to_options(params, "ram", "1");
         add_to_options(params, "vram", "1");
         add_to_options(params, "battery", "1");
         add_to_options(params, "hud_no_margin", "1");
         add_to_options(params, "gpu_power", "1");
         add_to_options(params, "cpu_power", "1");
         add_to_options(params, "battery_watt", "1");
         add_to_options(params, "battery_time", "1");
         add_to_options(params, "debug", "0");
         break;

      case 3:
         add_to_options(params, "cpu_temp", "1");
         add_to_options(params, "gpu_temp", "1");
         add_to_options(params, "ram", "1");
         add_to_options(params, "vram", "1");
         add_to_options(params, "cpu_power", "1");
         add_to_options(params, "gpu_power", "1");
         add_to_options(params, "cpu_mhz", "1");
         add_to_options(params, "gpu_mem_clock", "1");
         add_to_options(params, "gpu_core_clock", "1");
         add_to_options(params, "battery", "1");
         add_to_options(params, "hdr", "1");
         add_to_options(params, "debug", "0");
         break;

      case 4:
         add_to_options(params, "full", "1");
         add_to_options(params, "throttling_status", "0");
         add_to_options(params, "throttling_status_graph", "0");
         add_to_options(params, "io_read", "0");
         add_to_options(params, "io_write", "0");
         add_to_options(params, "arch", "0");
         add_to_options(params, "engine_version", "0");
         add_to_options(params, "battery", "1");
         add_to_options(params, "gamemode", "0");
         add_to_options(params, "vkbasalt", "0");
         add_to_options(params, "frame_count", "0");
         add_to_options(params, "show_fps_limit", "0");
         add_to_options(params, "resolution", "0");
         add_to_options(params, "gpu_load_change", "0");
         add_to_options(params, "core_load_change", "0");
         add_to_options(params, "cpu_load_change", "0");
         add_to_options(params, "fps_color_change", "0");
         add_to_options(params, "hdr", "1");
         add_to_options(params, "refresh_rate", "1");
         add_to_options(params, "media_player", "0");
         add_to_options(params, "debug", "1");
         add_to_options(params, "version", "0");
         add_to_options(params, "frame_timing_detailed", "1");
         break;

   }
}
