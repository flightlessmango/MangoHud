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
#include "fex.h"
#include "ftrace.h"

#if defined(HAVE_X11) || defined(HAVE_WAYLAND)
#include <xkbcommon/xkbcommon.h>
#endif

#include "dbus_info.h"

#include "app/mangoapp.h"
#include "fps_metrics.h"
#include "version.h"

std::unique_ptr<fpsMetrics> fpsmetrics;
std::mutex config_mtx;
std::condition_variable config_cv;
bool config_ready = false;
static std::shared_ptr<overlay_params> g_params;

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
      SPDLOG_DEBUG("Couldn't create socket pipe at '{}'", path);
      SPDLOG_DEBUG("ERROR: '{}'", strerror(errno));
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

#if defined(HAVE_X11) || defined(HAVE_WAYLAND)
static std::vector<KeySym>
parse_string_to_keysym_vec(const char *str)
{
   std::vector<KeySym> keys;
   auto keyStrings = str_tokenize(str);
   for (auto& ks : keyStrings) {
      trim(ks);
      xkb_keysym_t xk = xkb_keysym_from_name(ks.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
      if (xk != XKB_KEY_NoSymbol)
         keys.push_back(xk);
      else
         SPDLOG_ERROR("Unrecognized key: '{}'", ks);
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

static std::vector<float>
parse_fps_limit(const char *str)
{
   std::vector<float> fps_limit;
   auto fps_limit_strings = str_tokenize(str);

   for (auto& value : fps_limit_strings) {
      trim(value);

      float as_float;
      try {
         as_float = std::stof(value);
      } catch (const std::invalid_argument&) {
         SPDLOG_ERROR("invalid fps_limit value: '{}'", value);
         continue;
      } catch (const std::out_of_range&) {
         SPDLOG_ERROR("fps_limit value out of range: '{}'", value);
         continue;
      }

      fps_limit.push_back(as_float);
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

   // pad vec with white color so we always have at least 3
   while (load_colors.size() < 3) {
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

static std::vector<unsigned>
parse_gpu_list(const char *str) {

   std::vector<unsigned int> result;
   std::stringstream ss{std::string(str)};
   std::string item;

   while (std::getline(ss, item, ',')) {
      unsigned int num = static_cast<unsigned int>(std::stoul(item));
      result.push_back(num);
   }

   return result;
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
   SPDLOG_INFO("benchmark_percetile is obsolete and will be removed. Use fps_metrics instead");
   std::vector<std::string> percentiles;
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

static overlay_params::fex_stats_options
parse_fex_stats(const char *str) {
   overlay_params::fex_stats_options options {
#ifdef HAVE_FEX
      .enabled = fex::is_fex_capable(),
#endif
   };

   auto tokens = str_tokenize(str);
#define option_check(str, option) do { \
      if (token == #str) options.option = true; \
   } while (0)

   // If we have any tokens then default disable.
   if (!tokens.empty()) {
      options.status = false;
      options.app_type = false;
      options.hot_threads = false;
      options.jit_load = false;
      options.sigbus_counts = false;
      options.smc_counts = false;
      options.softfloat_counts = false;
   }

   for (auto& token : tokens) {
      option_check(status, status);
      option_check(apptype, app_type);
      option_check(hotthreads, hot_threads);
      option_check(jitload, jit_load);
      option_check(sigbus, sigbus_counts);
      option_check(smc, smc_counts);
      option_check(softfloat, softfloat_counts);
   }

   return options;
}

static overlay_params::ftrace_options
parse_ftrace(const char *str) {
   overlay_params::ftrace_options options;
#ifdef HAVE_FTRACE
   auto ftrace_params = str_tokenize(str, "+");
   for (auto& param : ftrace_params) {
      auto tokenized_param = str_tokenize(param, "/");
      if (tokenized_param.empty()) {
         SPDLOG_ERROR("Failed to parse ftrace parameter '{}'", param);
         continue;
      }

      if (tokenized_param[0] == "histogram") {
         if (tokenized_param.size() != 2) {
            SPDLOG_ERROR("Failed to parse ftrace histogram parameter '{}'", param);
            continue;
         }

         SPDLOG_DEBUG("Using ftrace histogram for '{}'", tokenized_param[1]);
         options.tracepoints.push_back(std::make_shared<FTrace::Tracepoint>(
            FTrace::Tracepoint {
               .name = tokenized_param[1],
               .type = FTrace::TracepointType::Histogram,
            }));
      } else if (tokenized_param[0] == "linegraph") {
         if (tokenized_param.size() != 3) {
            SPDLOG_ERROR("Failed to parse ftrace linegraph parameter '{}'", param);
            continue;
         }

         SPDLOG_DEBUG("Using ftrace line graph for '{}'", tokenized_param[1]);
         options.tracepoints.push_back(std::make_shared<FTrace::Tracepoint>(
            FTrace::Tracepoint {
               .name = tokenized_param[1],
               .type = FTrace::TracepointType::LineGraph,
               .field_name = tokenized_param[2],
            }));
      } else if (tokenized_param[0] == "label") {
         if (tokenized_param.size() != 3) {
            SPDLOG_ERROR("Failed to parse ftrace label parameter '{}'", param);
            continue;
         }

         SPDLOG_DEBUG("Using ftrace label for '{}', label name '{}'", tokenized_param[1], tokenized_param[2]);
         options.tracepoints.push_back(std::make_shared<FTrace::Tracepoint>(
            FTrace::Tracepoint {
               .name = tokenized_param[1],
               .type = FTrace::TracepointType::Label,
               .field_name = tokenized_param[2],
            }));
      } else {
         SPDLOG_ERROR("Failed to parse ftrace parameter '{}'", param);
      }
   }

   options.enabled = !options.tracepoints.empty();
#endif // HAVE_FTRACE
   return options;
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
#define parse_fps_text(s) parse_str(s)
#define parse_log_interval(s) parse_unsigned(s)
#define parse_font_size(s) parse_float(s)
#define parse_font_size_secondary(s) parse_float(s)
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
#define parse_horizontal_separator_color(s) parse_color(s)
#define parse_network_color(s) parse_color(s)
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
#define parse_network(s) parse_str_tokenize(s)
#define parse_gpu_text(s) parse_str_tokenize(s)

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
initialize_preset(struct overlay_params *params)
{
   if (params->options.find("preset") != params->options.end()) {
      auto presets = parse_preset(params->options.find("preset")->second.c_str());
      if (!presets.empty())
         params->preset = presets;
   }
   current_preset = params->preset[0];
}

static void
set_parameters_from_options(struct overlay_params *params)
{
   bool read_cfg = false;
   if (params->options.find("read_cfg") != params->options.end() && params->options.find("read_cfg")->second != "0")
      read_cfg = true;

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
      params->enabled[OVERLAY_PARAM_ENABLED_hide_engine_names] = 0;
      params->enabled[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = 0;
      params->enabled[OVERLAY_PARAM_ENABLED_dynamic_frame_timing] = 0;
      params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit] = 0;
      params->enabled[OVERLAY_PARAM_ENABLED_duration] = false;
      params->enabled[OVERLAY_PARAM_ENABLED_core_bars] = false;
      params->enabled[OVERLAY_PARAM_ENABLED_read_cfg] = read_cfg;
      params->enabled[OVERLAY_PARAM_ENABLED_time_no_label] = false;
      params->enabled[OVERLAY_PARAM_ENABLED_core_type] = false;
      params->options.erase("full");
   }
   for (auto& it : params->options) {
#define OVERLAY_PARAM_BOOL(name)                                 \
      if (it.first == #name) {                                   \
          params->enabled[OVERLAY_PARAM_ENABLED_##name] =        \
          strtol(it.second.c_str(), NULL, 0);                    \
          continue;                                              \
       }
#define OVERLAY_PARAM_CUSTOM(name)                               \
      if (it.first == #name) {                                   \
         params->name = parse_##name(it.second.c_str());         \
      continue;                                                  \
   }
      OVERLAY_PARAMS
      #undef OVERLAY_PARAM_BOOL
      #undef OVERLAY_PARAM_CUSTOM
      if (it.first == "preset") {
         continue; // Handled above
      }
      SPDLOG_ERROR("Unknown option '{}'", it.first.c_str());
   }
}

static void
parse_overlay_env(struct overlay_params *params,
                  const char *env, bool use_existing_preset)
{
   const char *env_start = env;

   uint32_t num;
   char key[256], value[256];
   while ((num = parse_string(env, key, value)) != 0) {
      trim_char(key);
      trim_char(value);
      env += num;
      if (!strcmp("preset", key)) {
         if (!use_existing_preset) {
            add_to_options(params, key, value);
            initialize_preset(params);
         }
         break;
      }
   }

   presets(current_preset, params);
   env = env_start;

   while ((num = parse_string(env, key, value)) != 0) {
      trim_char(key);
      trim_char(value);
      env += num;
      if (!strcmp("preset", key)) {
         continue; // Avoid 'Unknown option' error
      }
#define OVERLAY_PARAM_BOOL(name)                                       \
      if (!strcmp(#name, key)) {                                       \
         add_to_options(params, key, value);                           \
         continue;                                                     \
      }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
      if (!strcmp(#name, key)) {                                       \
         add_to_options(params, key, value);                           \
         continue;                                                     \
      }
      OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      SPDLOG_ERROR("Unknown option '{}'", key);
   }
   set_parameters_from_options(params);
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
   params->enabled[OVERLAY_PARAM_ENABLED_hide_engine_names] = false;
   params->enabled[OVERLAY_PARAM_ENABLED_hide_fps_superscript] = false;
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
   params->network_color = 0xe07b85;
   params->media_player_name = "";
   params->font_scale = 1.0f;
   params->wine_color = 0xeb5b5b;
   params->horizontal_separator_color = 0xad64c1;
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

static std::string verify_pci_dev(std::string pci_dev) {
   uint32_t domain, bus, slot, func;

   if (
      sscanf(
         pci_dev.c_str(), "%04x:%02x:%02x.%x",
         &domain, &bus, &slot, &func
      ) != 4) {
      SPDLOG_ERROR("Failed to parse PCI device ID: '{}'", pci_dev);
      return pci_dev;
   }

   std::stringstream ss;
   ss << std::hex
      << std::setw(4) << std::setfill('0') << domain << ":"
      << std::setw(2) << bus << ":"
      << std::setw(2) << slot << "."
      << std::setw(1) << func;

   SPDLOG_DEBUG("pci_dev = {}", ss.str());
   return ss.str();
}

void
parse_overlay_config(struct overlay_params *params,
                  const char *env, bool use_existing_preset)
{
   SPDLOG_DEBUG("Version: {}", MANGOHUD_VERSION);
   std::vector<int> default_preset = {-1, 0, 1, 2, 3, 4};
   auto preset = std::move(params->preset);
   *params = {};
   params->preset = use_existing_preset ? std::move(preset) : default_preset;
   set_param_defaults(params);
   if (!use_existing_preset) {
      current_preset = params->preset[0];
   }

#if defined(HAVE_X11) || defined(HAVE_WAYLAND)
   params->toggle_hud = { XKB_KEY_Shift_R, XKB_KEY_F12 };
   params->toggle_hud_position = { XKB_KEY_Shift_R, XKB_KEY_F11 };
   params->toggle_preset = { XKB_KEY_Shift_R, XKB_KEY_F10 };
   params->reset_fps_metrics = { XKB_KEY_Shift_R, XKB_KEY_F9};
   params->toggle_fps_limit = { XKB_KEY_Shift_L, XKB_KEY_F1 };
   params->toggle_logging = { XKB_KEY_Shift_L, XKB_KEY_F2 };
   params->reload_cfg = { XKB_KEY_Shift_L, XKB_KEY_F4 };
   params->upload_log = { XKB_KEY_Shift_L, XKB_KEY_F3 };
   params->upload_logs = { XKB_KEY_Control_L, XKB_KEY_F3 };
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
   params->options.clear();
   HUDElements.options.clear();

   // first pass with env var
   if (env)
      parse_overlay_env(params, env, use_existing_preset);

   bool read_cfg = params->enabled[OVERLAY_PARAM_ENABLED_read_cfg];
   bool env_contains_preset = params->options.find("preset") != params->options.end();

   if (!env || read_cfg) {
      parseConfigFile(*params);

      if (!use_existing_preset && !env_contains_preset) {
         initialize_preset(params);
      }

      // clear options since we don't want config options to appear first
      params->options.clear();
      HUDElements.options.clear();
      // add preset options
      presets(current_preset, params);
      // potentially override preset options with config options
      parseConfigFile(*params);

      set_parameters_from_options(params);
   }

   if (params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout])
      HUDElements.ordered_functions.clear();

   if (env && read_cfg) {
      HUDElements.ordered_functions.clear();
      parse_overlay_env(params, env, true);
   }

   // If fps_only param is enabled disable legacy_layout
   if (params->enabled[OVERLAY_PARAM_ENABLED_fps_only])
      params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout] = false;

   if (is_blacklisted())
      return;

   if (params->font_scale_media_player <= 0.f)
      params->font_scale_media_player = 0.55f;

   // Convert from 0xRRGGBB to ImGui's format
   std::array<unsigned *, 24> colors = {
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
      &params->horizontal_separator_color,
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
      &params->network_color,
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

   // If secondary font size not set, compute it from main font_size
   if (!params->font_size_secondary)
      params->font_size_secondary = params->font_size * 0.55f;

   params->font_params_hash = get_hash(params->font_size,
                                 params->font_size_text,
                                 params->no_small_font,
                                 params->font_file,
                                 params->font_file_text,
                                 params->font_glyph_ranges,
                                 params->font_scale,
                                 params->font_size_secondary
                                );

   // check if user specified an env for fps limiter instead
   if (params->fps_limit.size() == 0 ||
      (params->fps_limit.size() == 1 && params->fps_limit[0] == 0))
   {
      const char *env = getenv("MANGOHUD_FPS_LIMIT");
      if (env)
      {
         try {
            int fps = std::stof(env);
            if (params->fps_limit.size() == 0)
               params->fps_limit.push_back(fps);
            else
               params->fps_limit[0] = fps;
         } catch(...) {}
      }
   }
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

   for (const auto& option : HUDElements.options) {
      SPDLOG_DEBUG("Param: '{}' = '{}'", option.first, option.second);
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
   if (HUDElements.net)
      HUDElements.net->should_reset = true;

   if (!params->gpu_list.empty() && !params->pci_dev.empty()) {
      SPDLOG_WARN(
         "You have specified both gpu_list and pci_dev, "
         "ignoring pci_dev."
      );
   }

   if (!params->pci_dev.empty())
      params->pci_dev = verify_pci_dev(params->pci_dev);

   {
      std::lock_guard<std::mutex> lock(config_mtx);
      config_ready = true;
      config_cv.notify_one();
   }
   
   auto snapshot = std::make_shared<overlay_params>(*params);
   std::atomic_store_explicit(&g_params, std::move(snapshot), std::memory_order_release);
   HUDElements.params = get_params();

   if (!gpus)
      gpus = std::make_unique<GPUS>();

   if (params->enabled[OVERLAY_PARAM_ENABLED_legacy_layout]) {
      HUDElements.legacy_elements();
   } else {
      HUDElements.ordered_functions.clear();
      for (auto& option : HUDElements.options) {
         HUDElements.sort_elements(option);
      }
   }
}

std::shared_ptr<overlay_params> get_params() {
    return std::atomic_load_explicit(&g_params, std::memory_order_acquire);
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
      SPDLOG_DEBUG("Failed to read presets file: '{}'.  Falling back to default presets", preset_path);
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
         add_to_options(params, "network", "1");
         add_to_options(params, "present_mode", "0");
         
         // Disable some options if steamdeck / other known handhelds
         for (auto gpu : gpus->available_gpus) {
            if (gpu->device_id == 0x1435 || gpu->device_id == 0x163f || gpu->device_id == 0x1681 || gpu->device_id == 0x15bf){
               add_to_options(params, "gpu_fan", "0");
               add_to_options(params, "gpu_junction_temp", "0");
               add_to_options(params, "gpu_voltage", "0");
               add_to_options(params, "gpu_mem_temp", "0");
               add_to_options(params, "gpu_efficiency", "0");
            }
            // Rembrandt and Phoenix APUs (Z1, Z1E, Z2 Go)
            if (gpu->device_id == 0x1681 || gpu->device_id == 0x15bf){
               add_to_options(params, "gpu_power_limit", "0");
            }
         }


         break;

   }
}
