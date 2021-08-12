#include <sstream>
#include <iomanip>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "overlay.h"
#include "logging.h"
#include "cpu.h"
#include "gpu.h"
#include "memory.h"
#include "timing.hpp"
#include "mesa/util/macros.h"
#include "string_utils.h"
#include "battery.h"
#include "string_utils.h"
#include "file_utils.h"
#include "gpu.h"
#include "logging.h"
#include "cpu.h"
#include "memory.h"
#include "pci_ids.h"
#include "timing.hpp"

#ifdef __gnu_linux__
#include <libgen.h>
#include <unistd.h>
#endif

#ifdef HAVE_DBUS
float g_overflow = 50.f /* 3333ms * 0.5 / 16.6667 / 2 (to edge and back) */;
#endif

string gpuString,wineVersion,wineProcess;
uint32_t deviceID;
bool gui_open = false;
struct benchmark_stats benchmark;
struct fps_limit fps_limit_stats {};
ImVec2 real_font_size;
std::vector<logData> graph_data;
const char* engines[] = {"Unknown", "OpenGL", "VULKAN", "DXVK", "VKD3D", "DAMAVAND", "ZINK", "WINED3D", "Feral3D", "ToGL"};

void update_hw_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID)
{
#ifdef __gnu_linux__
   if (params.enabled[OVERLAY_PARAM_ENABLED_battery]) {
      Battery_Stats.update();
   }
#endif
   if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_stats] || logger->is_active()) {
      cpuStats.UpdateCPUData();
#ifdef __gnu_linux__

      if (params.enabled[OVERLAY_PARAM_ENABLED_core_load] || params.enabled[OVERLAY_PARAM_ENABLED_cpu_mhz])
         cpuStats.UpdateCoreMhz();
      if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_temp] || logger->is_active() || params.enabled[OVERLAY_PARAM_ENABLED_graphs])
         cpuStats.UpdateCpuTemp();
      if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_power])
         cpuStats.UpdateCpuPower();
#endif
   }
   if (params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] || logger->is_active()) {
      if (vendorID == 0x1002 && getAmdGpuInfo_actual)
         getAmdGpuInfo_actual();

      if (vendorID == 0x10de)
         getNvidiaGpuInfo();
   }

   // get ram usage/max

#ifdef __gnu_linux__
   if (params.enabled[OVERLAY_PARAM_ENABLED_ram] || params.enabled[OVERLAY_PARAM_ENABLED_swap] || logger->is_active())
      update_meminfo();
   if (params.enabled[OVERLAY_PARAM_ENABLED_procmem])
      update_procmem();
   if (params.enabled[OVERLAY_PARAM_ENABLED_io_read] || params.enabled[OVERLAY_PARAM_ENABLED_io_write])
      getIoStats(&sw_stats.io);
#endif

   currentLogData.gpu_load = gpu_info.load;
   currentLogData.gpu_temp = gpu_info.temp;
   currentLogData.gpu_core_clock = gpu_info.CoreClock;
   currentLogData.gpu_mem_clock = gpu_info.MemClock;
   currentLogData.gpu_vram_used = gpu_info.memoryUsed;
   currentLogData.gpu_power = gpu_info.powerUsage;
#ifdef __gnu_linux__
   currentLogData.ram_used = memused;
#endif

   currentLogData.cpu_load = cpuStats.GetCPUDataTotal().percent;
   currentLogData.cpu_temp = cpuStats.GetCPUDataTotal().temp;
   // Save data for graphs
   if (graph_data.size() > 50)
      graph_data.erase(graph_data.begin());
   graph_data.push_back(currentLogData);
   logger->notify_data_valid();
   HUDElements.update_exec();
}

void update_hud_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID){
   uint32_t f_idx = sw_stats.n_frames % ARRAY_SIZE(sw_stats.frames_stats);
   uint64_t now = os_time_get_nano(); /* ns */
   double elapsed = (double)(now - sw_stats.last_fps_update); /* ns */
   fps = 1000000000.0 * sw_stats.n_frames_since_update / elapsed;
   if (logger->is_active())
      benchmark.fps_data.push_back(fps);

   if (sw_stats.last_present_time) {
        sw_stats.frames_stats[f_idx].stats[OVERLAY_PLOTS_frame_timing] =
            now - sw_stats.last_present_time;
   }

   frametime = (now - sw_stats.last_present_time) / 1000;
   if (elapsed >= params.fps_sampling_period) {
      std::thread(update_hw_info, std::ref(sw_stats), std::ref(params), vendorID).detach();
      sw_stats.fps = fps;

      if (params.enabled[OVERLAY_PARAM_ENABLED_time]) {
         std::time_t t = std::time(nullptr);
         std::stringstream time;
         time << std::put_time(std::localtime(&t), params.time_format.c_str());
         sw_stats.time = time.str();
      }

      sw_stats.n_frames_since_update = 0;
      sw_stats.last_fps_update = now;

   }

   if (params.log_interval == 0){
      logger->try_log();
   }

   sw_stats.last_present_time = now;
   sw_stats.n_frames++;
   sw_stats.n_frames_since_update++;
}

void calculate_benchmark_data(void *params_void){
   overlay_params *params = reinterpret_cast<overlay_params *>(params_void);

   vector<float> sorted = benchmark.fps_data;
   std::sort(sorted.begin(), sorted.end());
   benchmark.percentile_data.clear();

   benchmark.total = 0.f;
   for (auto fps_ : sorted){
      benchmark.total = benchmark.total + fps_;
   }

   size_t max_label_size = 0;

   for (std::string percentile : params->benchmark_percentiles) {
      float result;

      // special case handling for a mean-based average
      if (percentile == "AVG") {
         result = benchmark.total / sorted.size();
      } else {
         // the percentiles are already validated when they're parsed from the config.
         float fraction = parse_float(percentile) / 100;

         result = sorted[(fraction * sorted.size()) - 1];
         percentile += "%";
      }

      if (percentile.length() > max_label_size)
         max_label_size = percentile.length();

      benchmark.percentile_data.push_back({percentile, result});
   }

   for (auto& entry : benchmark.percentile_data) {
      entry.first.append(max_label_size - entry.first.length(), ' ');
   }
}


float get_time_stat(void *_data, int _idx)
{
   struct swapchain_stats *data = (struct swapchain_stats *) _data;
   if ((ARRAY_SIZE(data->frames_stats) - _idx) > data->n_frames)
      return 0.0f;
   int idx = ARRAY_SIZE(data->frames_stats) +
      data->n_frames < ARRAY_SIZE(data->frames_stats) ?
      _idx - data->n_frames :
      _idx + data->n_frames;
   idx %= ARRAY_SIZE(data->frames_stats);
   /* Time stats are in us. */
   return data->frames_stats[idx].stats[data->stat_selector] / data->time_dividor;
}

void position_layer(struct swapchain_stats& data, struct overlay_params& params, ImVec2 window_size)
{
   unsigned width = ImGui::GetIO().DisplaySize.x;
   unsigned height = ImGui::GetIO().DisplaySize.y;
   float margin = 10.0f;
   if (params.offset_x > 0 || params.offset_y > 0)
      margin = 0.0f;

   ImGui::SetNextWindowBgAlpha(params.background_alpha);
   ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,-3));
   ImGui::PushStyleVar(ImGuiStyleVar_Alpha, params.alpha);
   switch (params.position) {
   case LAYER_POSITION_TOP_LEFT:
      data.main_window_pos = ImVec2(margin + params.offset_x, margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_TOP_RIGHT:
      data.main_window_pos = ImVec2(width - window_size.x - margin + params.offset_x, margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_MIDDLE_LEFT:
      data.main_window_pos = ImVec2(margin + params.offset_x, height / 2 - window_size.y / 2 - margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_MIDDLE_RIGHT:
      data.main_window_pos = ImVec2(width - window_size.x - margin + params.offset_x, height / 2 - window_size.y / 2 - margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_BOTTOM_LEFT:
      data.main_window_pos = ImVec2(margin + params.offset_x, height - window_size.y - margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_BOTTOM_RIGHT:
      data.main_window_pos = ImVec2(width - window_size.x - margin + params.offset_x, height - window_size.y - margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_TOP_CENTER:
      data.main_window_pos = ImVec2((width / 2) - (window_size.x / 2), margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   }
}

void right_aligned_text(ImVec4& col, float off_x, const char *fmt, ...)
{
   ImVec2 pos = ImGui::GetCursorPos();
   char buffer[32] {};

   va_list args;
   va_start(args, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);

   ImVec2 sz = ImGui::CalcTextSize(buffer);
   ImGui::SetCursorPosX(pos.x + off_x - sz.x);
   //ImGui::Text("%s", buffer);
   ImGui::TextColored(col,"%s",buffer);
}

void center_text(const std::string& text)
{
   ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2 )- (ImGui::CalcTextSize(text.c_str()).x / 2));
}

float get_ticker_limited_pos(float pos, float tw, float& left_limit, float& right_limit)
{
   //float cw = ImGui::GetContentRegionAvailWidth() * 3; // only table cell worth of width
   float cw = ImGui::GetWindowContentRegionMax().x - ImGui::GetStyle().WindowPadding.x;
   float new_pos_x = ImGui::GetCursorPosX();
   left_limit = cw - tw + new_pos_x;
   right_limit = new_pos_x;

   if (cw < tw) {
      new_pos_x += pos;
      // acts as a delay before it starts scrolling again
      if (new_pos_x < left_limit)
         return left_limit;
      else if (new_pos_x > right_limit)
         return right_limit;
      else
         return new_pos_x;
   }
   return new_pos_x;
}

#ifdef HAVE_DBUS
void render_mpris_metadata(struct overlay_params& params, mutexed_metadata& meta, uint64_t frame_timing)
{
   if (meta.meta.valid) {
      auto color = ImGui::ColorConvertU32ToFloat4(params.media_player_color);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,0));
      ImGui::Dummy(ImVec2(0.0f, 20.0f));
      //ImGui::PushFont(data.font1);

      if (meta.ticker.needs_recalc) {
         meta.ticker.tw0 = ImGui::CalcTextSize(meta.meta.title.c_str()).x;
         meta.ticker.tw1 = ImGui::CalcTextSize(meta.meta.artists.c_str()).x;
         meta.ticker.tw2 = ImGui::CalcTextSize(meta.meta.album.c_str()).x;
         meta.ticker.longest = std::max(std::max(
               meta.ticker.tw0,
               meta.ticker.tw1),
            meta.ticker.tw2);
         meta.ticker.needs_recalc = false;
      }

      float new_pos, left_limit = 0, right_limit = 0;
      get_ticker_limited_pos(meta.ticker.pos, meta.ticker.longest, left_limit, right_limit);

      if (meta.ticker.pos < left_limit - g_overflow * .5f) {
         meta.ticker.dir = -1;
         meta.ticker.pos = (left_limit - g_overflow * .5f) + 1.f /* random */;
      } else if (meta.ticker.pos > right_limit + g_overflow) {
         meta.ticker.dir = 1;
         meta.ticker.pos = (right_limit + g_overflow) - 1.f /* random */;
      }

      meta.ticker.pos -= .5f * (frame_timing / 16666666.7f /* ns */) * meta.ticker.dir;

      for (auto order : params.media_player_order) {
         switch (order) {
            case MP_ORDER_TITLE:
            {
               new_pos = get_ticker_limited_pos(meta.ticker.pos, meta.ticker.tw0, left_limit, right_limit);
               ImGui::SetCursorPosX(new_pos);
               ImGui::TextColored(color, "%s", meta.meta.title.c_str());
            }
            break;
            case MP_ORDER_ARTIST:
            {
               new_pos = get_ticker_limited_pos(meta.ticker.pos, meta.ticker.tw1, left_limit, right_limit);
               ImGui::SetCursorPosX(new_pos);
               ImGui::TextColored(color, "%s", meta.meta.artists.c_str());
            }
            break;
            case MP_ORDER_ALBUM:
            {
               //ImGui::NewLine();
               if (!meta.meta.album.empty()) {
                  new_pos = get_ticker_limited_pos(meta.ticker.pos, meta.ticker.tw2, left_limit, right_limit);
                  ImGui::SetCursorPosX(new_pos);
                  ImGui::TextColored(color, "%s", meta.meta.album.c_str());
               }
            }
            break;
            default: break;
         }
      }

      if (!meta.meta.playing) {
         ImGui::TextColored(color, "(paused)");
      }

      //ImGui::PopFont();
      ImGui::PopStyleVar();
   }
}
#endif

void render_benchmark(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, unsigned height, Clock::time_point now){
   // TODO, FIX LOG_DURATION FOR BENCHMARK
   int benchHeight = (2 + benchmark.percentile_data.size()) * real_font_size.x + 10.0f + 58;
   ImGui::SetNextWindowSize(ImVec2(window_size.x, benchHeight), ImGuiCond_Always);
   if (height - (window_size.y + data.main_window_pos.y + 5) < benchHeight)
      ImGui::SetNextWindowPos(ImVec2(data.main_window_pos.x, data.main_window_pos.y - benchHeight - 5), ImGuiCond_Always);
   else
      ImGui::SetNextWindowPos(ImVec2(data.main_window_pos.x, data.main_window_pos.y + window_size.y + 5), ImGuiCond_Always);

   float display_time = std::chrono::duration<float>(now - logger->last_log_end()).count();
   static float display_for = 10.0f;
   float alpha;
   if (params.background_alpha != 0){
      if (display_for >= display_time){
         alpha = display_time * params.background_alpha;
         if (alpha >= params.background_alpha){
            ImGui::SetNextWindowBgAlpha(params.background_alpha);
         }else{
            ImGui::SetNextWindowBgAlpha(alpha);
         }
      } else {
         alpha = 6.0 - display_time * params.background_alpha;
         if (alpha >= params.background_alpha){
            ImGui::SetNextWindowBgAlpha(params.background_alpha);
         }else{
            ImGui::SetNextWindowBgAlpha(alpha);
         }
      }
   } else {
      if (display_for >= display_time){
         alpha = display_time * 0.0001;
         ImGui::SetNextWindowBgAlpha(params.background_alpha);
      } else {
         alpha = 6.0 - display_time * 0.0001;
         ImGui::SetNextWindowBgAlpha(params.background_alpha);
      }
   }

   ImGui::Begin("Benchmark", &gui_open, ImGuiWindowFlags_NoDecoration);
   static const char* finished = "Logging Finished";
   ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2 )- (ImGui::CalcTextSize(finished).x / 2));
   ImGui::TextColored(ImVec4(1.0, 1.0, 1.0, alpha / params.background_alpha), "%s", finished);
   ImGui::Dummy(ImVec2(0.0f, 8.0f));

   char duration[20];
   snprintf(duration, sizeof(duration), "Duration: %.1fs", std::chrono::duration<float>(logger->last_log_end() - logger->last_log_begin()).count());
   ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2 )- (ImGui::CalcTextSize(duration).x / 2));
   ImGui::TextColored(ImVec4(1.0, 1.0, 1.0, alpha / params.background_alpha), "%s", duration);
   for (auto& data_ : benchmark.percentile_data){
      char buffer[20];
      snprintf(buffer, sizeof(buffer), "%s %.1f", data_.first.c_str(), data_.second);
      ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2 )- (ImGui::CalcTextSize(buffer).x / 2));
      ImGui::TextColored(ImVec4(1.0, 1.0, 1.0, alpha / params.background_alpha), "%s %.1f", data_.first.c_str(), data_.second);
   }

   float max = *max_element(benchmark.fps_data.begin(), benchmark.fps_data.end());
   ImVec4 plotColor = HUDElements.colors.frametime;
   plotColor.w = alpha / params.background_alpha;
   ImGui::PushStyleColor(ImGuiCol_PlotLines, plotColor);
   ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0, 0.0, 0.0, alpha / params.background_alpha));
   ImGui::Dummy(ImVec2(0.0f, 8.0f));
   if (params.enabled[OVERLAY_PARAM_ENABLED_histogram])
      ImGui::PlotHistogram("", benchmark.fps_data.data(), benchmark.fps_data.size(), 0, "", 0.0f, max + 10, ImVec2(ImGui::GetContentRegionAvailWidth(), 50));
   else
      ImGui::PlotLines("", benchmark.fps_data.data(), benchmark.fps_data.size(), 0, "", 0.0f, max + 10, ImVec2(ImGui::GetContentRegionAvailWidth(), 50));
   ImGui::PopStyleColor(2);
   ImGui::End();
}

ImVec4 change_on_load_temp(LOAD_DATA& data, unsigned current)
{
   if (current >= data.high_load){
      return data.color_high;
   }
   else if (current >= data.med_load){
      float diff = float(current - data.med_load) / float(data.high_load - data.med_load);
      float x = (data.color_high.x - data.color_med.x) * diff;
      float y = (data.color_high.y - data.color_med.y) * diff;
      float z = (data.color_high.z - data.color_med.z) * diff;
      return ImVec4(data.color_med.x + x, data.color_med.y + y, data.color_med.z + z, 1.0);
   } else {
      float diff = float(current) / float(data.med_load);
      float x = (data.color_med.x - data.color_low.x) * diff;
      float y = (data.color_med.y - data.color_low.y) * diff;
      float z = (data.color_med.z - data.color_low.z) * diff;
      return ImVec4(data.color_low.x + x, data.color_low.y + y, data.color_low.z + z, 1.0);
   }
}

void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, bool is_vulkan)
{
   HUDElements.sw_stats = &data; HUDElements.params = &params;
   HUDElements.is_vulkan = is_vulkan;
   ImGui::GetIO().FontGlobalScale = params.font_scale;
   if(!logger) logger = std::make_unique<Logger>(&params);
   static float ralign_width = 0, old_scale = 0;
   window_size = ImVec2(params.width, params.height);
   unsigned height = ImGui::GetIO().DisplaySize.y;
   auto now = Clock::now();

   if (old_scale != params.font_scale) {
      HUDElements.ralign_width = ralign_width = ImGui::CalcTextSize("A").x * 4 /* characters */;
      old_scale = params.font_scale;
   }

   if (!params.no_display){
      ImGui::Begin("Main", &gui_open, ImGuiWindowFlags_NoDecoration);
      ImGui::BeginTable("hud", params.table_columns, ImGuiTableFlags_NoClip);
      HUDElements.place = 0;
      for (auto& func : HUDElements.ordered_functions){
         func.first();
         HUDElements.place += 1;
      }
      ImGui::EndTable();

      if(logger->is_active())
         ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(data.main_window_pos.x + window_size.x - 15, data.main_window_pos.y + 15), 10, params.engine_color, 20);
      window_size = ImVec2(window_size.x, ImGui::GetCursorPosY() + 10.0f);
      ImGui::End();
      if((now - logger->last_log_end()) < 12s)
         render_benchmark(data, params, window_size, height, now);
   }
}

void init_cpu_stats(overlay_params& params)
{
#ifdef __gnu_linux__
   auto& enabled = params.enabled;
   enabled[OVERLAY_PARAM_ENABLED_cpu_stats] = cpuStats.Init()
                           && enabled[OVERLAY_PARAM_ENABLED_cpu_stats];
   enabled[OVERLAY_PARAM_ENABLED_cpu_temp] = cpuStats.GetCpuFile()
                           && enabled[OVERLAY_PARAM_ENABLED_cpu_temp];
   enabled[OVERLAY_PARAM_ENABLED_cpu_power] = cpuStats.InitCpuPowerData()
                           && enabled[OVERLAY_PARAM_ENABLED_cpu_power];
#endif
}

struct pci_bus {
   int domain;
   int bus;
   int slot;
   int func;
};

void init_gpu_stats(uint32_t& vendorID, uint32_t target_device_id, overlay_params& params)
{
   //if (!params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats])
   //   return;

   pci_bus pci;
   bool pci_bus_parsed = false;
   const char *pci_dev = nullptr;
   if (!params.pci_dev.empty())
      pci_dev = params.pci_dev.c_str();

   // for now just checks if pci bus parses correctly, if at all necessary
   if (pci_dev) {
      if (sscanf(pci_dev, "%04x:%02x:%02x.%x",
               &pci.domain, &pci.bus,
               &pci.slot, &pci.func) == 4) {
         pci_bus_parsed = true;
         // reformat back to sysfs file name's and nvml's expected format
         // so config file param's value format doesn't have to be as strict
         std::stringstream ss;
         ss << std::hex
            << std::setw(4) << std::setfill('0') << pci.domain << ":"
            << std::setw(2) << pci.bus << ":"
            << std::setw(2) << pci.slot << "."
            << std::setw(1) << pci.func;
         params.pci_dev = ss.str();
         pci_dev = params.pci_dev.c_str();
         SPDLOG_DEBUG("PCI device ID: '{}'", pci_dev);
      } else {
         SPDLOG_ERROR("Failed to parse PCI device ID: '{}'", pci_dev);
         SPDLOG_ERROR("Specify it as 'domain:bus:slot.func'");
      }
   }

   // NVIDIA or Intel but maybe has Optimus
   if (vendorID == 0x8086
      || vendorID == 0x10de) {

      if(checkNvidia(pci_dev))
         vendorID = 0x10de;
      else
         params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
   }

#ifdef __gnu_linux__
   if (vendorID == 0x8086 || vendorID == 0x1002
       || gpu.find("Radeon") != std::string::npos
       || gpu.find("AMD") != std::string::npos) {
      string path;
      string drm = "/sys/class/drm/";
      getAmdGpuInfo_actual = getAmdGpuInfo;
      bool using_libdrm = false;

      auto dirs = ls(drm.c_str(), "card");
      for (auto& dir : dirs) {
         path = drm + dir;

         SPDLOG_DEBUG("device path check: {}/device/vendor", path);

         string device = read_line(path + "/device/device");
         deviceID = strtol(device.c_str(), NULL, 16); // OGL might fail so read from sysfs

         string vendor = read_line(path + "/device/vendor");
         trim(vendor);
         if (vendor != "0x1002")
            continue;

         if (pci_bus_parsed && pci_dev) {
            string pci_device = read_symlink((path + "/device").c_str());
            SPDLOG_DEBUG("PCI device symlink: '{}'", pci_device);
            if (!ends_with(pci_device, pci_dev)) {
               SPDLOG_WARN("skipping GPU, PCI ID doesn't match: {}", pci_device);
               continue;
            }
         }
         // Don't skip if intel, maybe there's a dgpu
         else if (vendorID != 0x8086 && target_device_id && target_device_id != deviceID)
         {
            SPDLOG_WARN("expected device id {:04X}, got {:04X}, skipping", target_device_id, deviceID);
            continue;
         }

         string module = get_basename(read_symlink(path + "/device/driver/module"));
         SPDLOG_DEBUG("using device path: '{}', module: '{}'", path, module);

         int idx = -1;
         //TODO make neater
         int res = sscanf(path.c_str(), "%*[^0-9]%d", &idx);
         (void)res;
         std::string dri_path = "/dev/dri/card" + std::to_string(idx);

         if (module == "amdgpu")
         {
#ifdef HAVE_LIBDRM_AMDGPU
            if (!params.enabled[OVERLAY_PARAM_ENABLED_force_amdgpu_hwmon] && res == 1 && amdgpu_open(dri_path.c_str())) {
               vendorID = 0x1002;
               using_libdrm = true;
               getAmdGpuInfo_actual = getAmdGpuInfo_libdrm;
               amdgpu_set_sampling_period(params.fps_sampling_period);

               SPDLOG_DEBUG("Using libdrm");
               // fall through and open sysfs handles for fallback or check DRM version beforehand
            } else if (!params.enabled[OVERLAY_PARAM_ENABLED_force_amdgpu_hwmon]) {
               SPDLOG_WARN("Failed to open device '/dev/dri/card{}' with libdrm, falling back to using hwmon sysfs.", idx);
            }
#endif
         }
#ifdef HAVE_LIBDRM
         else if (module == "radeon")
         {
            if (res == 1 && radeon_open(dri_path.c_str())) {
               vendorID = 0x1002;
               using_libdrm = true;
               getAmdGpuInfo_actual = getRadeonInfo_libdrm;
               radeon_set_sampling_period(params.fps_sampling_period);
            } else {
               SPDLOG_WARN("Failed to open device '/dev/dri/card{}' with libdrm.", idx);
               params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
               break;
            }
         }
#endif

         if (!file_exists(path + "/device/gpu_busy_percent"))
            continue;

         path += "/device";
         if (!amdgpu.busy)
            amdgpu.busy = fopen((path + "/gpu_busy_percent").c_str(), "r");
         if (!amdgpu.vram_total)
            amdgpu.vram_total = fopen((path + "/mem_info_vram_total").c_str(), "r");
         if (!amdgpu.vram_used)
            amdgpu.vram_used = fopen((path + "/mem_info_vram_used").c_str(), "r");

         path += "/hwmon/";
         string tempFolder;
         if (find_folder(path, "hwmon", tempFolder)) {
            if (!amdgpu.core_clock)
               amdgpu.core_clock = fopen((path + tempFolder + "/freq1_input").c_str(), "r");
            if (!amdgpu.memory_clock)
               amdgpu.memory_clock = fopen((path + tempFolder + "/freq2_input").c_str(), "r");
            if (!amdgpu.temp)
               amdgpu.temp = fopen((path + tempFolder + "/temp1_input").c_str(), "r");
            if (!amdgpu.power_usage)
               amdgpu.power_usage = fopen((path + tempFolder + "/power1_average").c_str(), "r");

            vendorID = 0x1002;
            break;
         }
      }

      // don't bother then
      if (!using_libdrm && !amdgpu.busy && !amdgpu.temp && !amdgpu.vram_total && !amdgpu.vram_used) {
         params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
      }
   }
#endif
   if (!params.permit_upload)
      SPDLOG_INFO("Uploading is disabled (permit_upload = 0)");
}

void init_system_info(){
   #ifdef __gnu_linux__
      const char* ld_preload = getenv("LD_PRELOAD");
      if (ld_preload)
         unsetenv("LD_PRELOAD");

      ram =  exec("cat /proc/meminfo | grep 'MemTotal' | awk '{print $2}'");
      trim(ram);
      cpu =  exec("cat /proc/cpuinfo | grep 'model name' | tail -n1 | sed 's/^.*: //' | sed 's/([^)]*)/()/g' | tr -d '(/)'");
      trim(cpu);
      kernel = exec("uname -r");
      trim(kernel);
      os = exec("cat /etc/*-release | grep 'PRETTY_NAME' | cut -d '=' -f 2-");
      os.erase(remove(os.begin(), os.end(), '\"' ), os.end());
      trim(os);
      cpusched = read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

      const char* mangohud_recursion = getenv("MANGOHUD_RECURSION");
      if (!mangohud_recursion) {
         setenv("MANGOHUD_RECURSION", "1", 1);
         driver = exec("glxinfo -B | grep 'OpenGL version' | sed 's/^.*: //' | sed 's/([^()]*)//g' | tr -s ' '");
         trim(driver);
         unsetenv("MANGOHUD_RECURSION");
      } else {
         driver = "MangoHud glxinfo recursion detected";
      }

// Get WINE version

      wineProcess = get_exe_path();
      auto n = wineProcess.find_last_of('/');
      string preloader = wineProcess.substr(n + 1);
      if (preloader == "wine-preloader" || preloader == "wine64-preloader") {
         // Check if using Proton
         if (wineProcess.find("/dist/bin/wine") != std::string::npos || wineProcess.find("/files/bin/wine") != std::string::npos) {
            stringstream ss;
            ss << dirname((char*)wineProcess.c_str()) << "/../../version";
            string protonVersion = ss.str();
            ss.str(""); ss.clear();
            ss << read_line(protonVersion);
            std::getline(ss, wineVersion, ' '); // skip first number string
            std::getline(ss, wineVersion, ' ');
            trim(wineVersion);
            string toReplace = "proton-";
            size_t pos = wineVersion.find(toReplace);
            if (pos != std::string::npos) {
               // If found replace
               wineVersion.replace(pos, toReplace.length(), "Proton ");
            }
            else {
               // If not found insert for non official proton builds
               wineVersion.insert(0, "Proton ");
            }
         }
         else {
            char *dir = dirname((char*)wineProcess.c_str());
            stringstream findVersion;
            findVersion << "\"" << dir << "/wine\" --version";
            const char *wine_env = getenv("WINELOADERNOEXEC");
            if (wine_env)
               unsetenv("WINELOADERNOEXEC");
            wineVersion = exec(findVersion.str());
            std::cout << "WINE VERSION = " << wineVersion << "\n";
            if (wine_env)
               setenv("WINELOADERNOEXEC", wine_env, 1);
         }
      }
      else {
           wineVersion = "";
      }
      // check for gamemode and vkbasalt
      stringstream ss;
      string line;
      auto pid = getpid();
      string path = "/proc/" + to_string(pid) + "/map_files/";
      auto files = exec("ls " + path);
      ss << files;
      while(std::getline(ss, line, '\n')){
         auto file = path + line;
         auto sym = read_symlink(file.c_str());
         if (sym.find("gamemode") != std::string::npos)
            HUDElements.gamemode_bol = true;
         if (sym.find("vkbasalt") != std::string::npos)
            HUDElements.vkbasalt_bol = true;
         if (HUDElements.gamemode_bol && HUDElements.vkbasalt_bol)
            break;
      }

      if (ld_preload)
         setenv("LD_PRELOAD", ld_preload, 1);

      SPDLOG_DEBUG("Ram:{}", ram);
      SPDLOG_DEBUG("Cpu:{}", cpu);
      SPDLOG_DEBUG("Kernel:{}", kernel);
      SPDLOG_DEBUG("Os:{}", os);
      SPDLOG_DEBUG("Gpu:{}", gpu);
      SPDLOG_DEBUG("Driver:{}", driver);
      SPDLOG_DEBUG("CPU Scheduler:{}", cpusched);
#endif
}

void get_device_name(int32_t vendorID, int32_t deviceID, struct swapchain_stats& sw_stats)
{
#ifdef __gnu_linux__
   if (pci_ids.find(vendorID) == pci_ids.end())
      parse_pciids();

   string desc = pci_ids[vendorID].second[deviceID].desc;
   size_t position = desc.find("[");
   if (position != std::string::npos) {
      desc = desc.substr(position);
      string chars = "[]";
      for (char c: chars)
         desc.erase(remove(desc.begin(), desc.end(), c), desc.end());
   }
   gpu = sw_stats.gpuName = desc;
   trim(sw_stats.gpuName); trim(gpu);
#endif
}
