#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem.h>
// #include <sys/stat.h>
#include "overlay.h"
#include "cpu.h"
#include "gpu.h"
#include "memory.h"
#include "timing.hpp"
#include "fcat.h"
#include "mesa/util/macros.h"
#include "battery.h"
#include "device.h"
#include "string_utils.h"
#include "file_utils.h"
#include "pci_ids.h"
#include "iostats.h"
#include "amdgpu.h"
#include "fps_metrics.h"
#include "intel.h"
#include "msm.h"

#ifdef __linux__
#include <libgen.h>
#include <unistd.h>
#endif

namespace fs = ghc::filesystem;
using namespace std;

string gpuString,wineVersion,wineProcess;
uint32_t deviceID;
bool gui_open = false;
bool fcat_open = false;
struct benchmark_stats benchmark;
struct fps_limit fps_limit_stats {};
ImVec2 real_font_size;
std::deque<logData> graph_data;
const char* engines[] = {"Unknown", "OpenGL", "VULKAN", "DXVK", "VKD3D", "DAMAVAND", "ZINK", "WINED3D", "Feral3D", "ToGL", "GAMESCOPE"};
const char* engines_short[] = {"Unknown", "OGL", "VK", "DXVK", "VKD3D", "DV", "ZINK", "WD3D", "Feral3D", "ToGL", "GS"};
overlay_params *_params {};
double min_frametime, max_frametime;
bool gpu_metrics_exists = false;
bool steam_focused = false;
vector<float> frametime_data(200,0.f);
int fan_speed;
fcatoverlay fcatstatus;
std::string drm_dev;
int current_preset;

void init_spdlog()
{
   if (spdlog::get("MANGOHUD"))
      return;

   spdlog::set_default_logger(spdlog::stderr_color_mt("MANGOHUD")); // Just to get the name in log
   if (getenv("MANGOHUD_USE_LOGFILE"))
   {
      try
      {
         // Not rotating when opening log as proton/wine create multiple (sub)processes
         auto log = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (get_config_dir() + "/MangoHud/MangoHud.log", 10*1024*1024, 5, false);
         spdlog::get("MANGOHUD")->sinks().push_back(log);
      }
      catch (const spdlog::spdlog_ex &ex)
      {
         SPDLOG_ERROR("{}", ex.what());
      }
   }
#ifdef DEBUG
   spdlog::set_level(spdlog::level::level_enum::debug);
#endif
   spdlog::cfg::load_env_levels();

   // Use MANGOHUD_LOG_LEVEL to correspond to SPDLOG_LEVEL
   if (getenv("MANGOHUD_LOG_LEVEL")) {
      std::string log_level = getenv("MANGOHUD_LOG_LEVEL");
      vector<string> levels;
      levels = {"off","info","err","debug"};
      for (auto & element : levels) {
         transform(log_level.begin(), log_level.end(), log_level.begin(), ::tolower);
         if(log_level == element ) {
            spdlog::set_level(spdlog::level::from_str(log_level));
         }
      }
#ifndef DEBUG
   } else {
      std::string log_level = "err";
      transform(log_level.begin(), log_level.end(), log_level.begin(), ::tolower);
      spdlog::set_level(spdlog::level::from_str(log_level));
#endif
   }

}

void FpsLimiter(struct fps_limit& stats){
   stats.sleepTime = stats.targetFrameTime - (stats.frameStart - stats.frameEnd);
   if (stats.sleepTime > stats.frameOverhead) {
      auto adjustedSleep = stats.sleepTime - stats.frameOverhead;
      this_thread::sleep_for(adjustedSleep);
      stats.frameOverhead = ((Clock::now() - stats.frameStart) - adjustedSleep);
      if (stats.frameOverhead > stats.targetFrameTime / 2)
         stats.frameOverhead = Clock::duration(0);
   }
}

void update_hw_info(const struct overlay_params& params, uint32_t vendorID)
{
   update_fan();
   if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_stats] || logger->is_active()) {
      cpuStats.UpdateCPUData();

#ifdef __linux__
      if (params.enabled[OVERLAY_PARAM_ENABLED_core_load] || params.enabled[OVERLAY_PARAM_ENABLED_cpu_mhz])
         cpuStats.UpdateCoreMhz();
      if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_temp] || logger->is_active() || params.enabled[OVERLAY_PARAM_ENABLED_graphs])
         cpuStats.UpdateCpuTemp();
      if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_power])
         cpuStats.UpdateCpuPower();
#endif
   }
   if (params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] || logger->is_active()) {
      if (vendorID == 0x1002)
         getAmdGpuInfo();
#ifdef __linux__
      if (gpu_metrics_exists)
         amdgpu_get_metrics(deviceID);
#endif
      if (vendorID == 0x10de)
         getNvidiaGpuInfo(params);
#ifdef __linux__
      if (vendorID== 0x8086)
         if (intel) intel->update();
      if (vendorID == 0x5143)
         if (msm) msm->update();
#endif
   }

#ifdef __linux__
   if (params.enabled[OVERLAY_PARAM_ENABLED_battery])
      Battery_Stats.update();
   if (!params.device_battery.empty()) {
      device_update(params);
      if (device_found) {
            device_info();
      }
   }
   if (params.enabled[OVERLAY_PARAM_ENABLED_ram] || params.enabled[OVERLAY_PARAM_ENABLED_swap] || logger->is_active())
      update_meminfo();
   if (params.enabled[OVERLAY_PARAM_ENABLED_procmem])
      update_procmem();
   if (params.enabled[OVERLAY_PARAM_ENABLED_io_read] || params.enabled[OVERLAY_PARAM_ENABLED_io_write])
      getIoStats(g_io_stats);
#endif

   currentLogData.gpu_load = gpu_info.load;
   currentLogData.gpu_temp = gpu_info.temp;
   currentLogData.gpu_core_clock = gpu_info.CoreClock;
   currentLogData.gpu_mem_clock = gpu_info.MemClock;
   currentLogData.gpu_vram_used = gpu_info.memoryUsed;
   currentLogData.gpu_power = gpu_info.powerUsage;
#ifdef __linux__
   currentLogData.ram_used = memused;
   currentLogData.swap_used = swapused;
   currentLogData.process_rss = proc_mem.resident / float((2 << 29)); // GiB, consistent w/ other mem stats
#endif

   currentLogData.cpu_load = cpuStats.GetCPUDataTotal().percent;
   currentLogData.cpu_temp = cpuStats.GetCPUDataTotal().temp;
   // Save data for graphs
   if (graph_data.size() >= kMaxGraphEntries)
      graph_data.pop_front();
   graph_data.push_back(currentLogData);
   if (logger) logger->notify_data_valid();
   HUDElements.update_exec();
}

struct hw_info_updater
{
   bool quit = false;
   std::thread thread {};
   const struct overlay_params* params = nullptr;
   uint32_t vendorID;
   bool update_hw_info_thread = false;

   std::condition_variable cv_hwupdate;
   std::mutex m_cv_hwupdate, m_hw_updating;

   hw_info_updater()
   {
      thread = std::thread(&hw_info_updater::run, this);
   }

   ~hw_info_updater()
   {
      quit = true;
      cv_hwupdate.notify_all();
      if (thread.joinable())
         thread.join();
   }

   void update(const struct overlay_params* params_, uint32_t vendorID_)
   {
      std::unique_lock<std::mutex> lk_hw_updating(m_hw_updating, std::try_to_lock);
      if (lk_hw_updating.owns_lock())
      {
         params = params_;
         vendorID = vendorID_;
         update_hw_info_thread = true;
         cv_hwupdate.notify_all();
      }
   }

   void run(){
      while (!quit){
         std::unique_lock<std::mutex> lk_cv_hwupdate(m_cv_hwupdate);
         cv_hwupdate.wait(lk_cv_hwupdate, [&]{ return update_hw_info_thread || quit; });
         if (quit) break;

         if (params)
         {
            std::unique_lock<std::mutex> lk_hw_updating(m_hw_updating);
            update_hw_info(*params, vendorID);
         }
         update_hw_info_thread = false;
      }
   }
};

static std::unique_ptr<hw_info_updater> hw_update_thread;

void stop_hw_updater()
{
   if (hw_update_thread)
      hw_update_thread.reset();
}

void update_hud_info_with_frametime(struct swapchain_stats& sw_stats, const struct overlay_params& params, uint32_t vendorID, uint64_t frametime_ns){
   uint32_t f_idx = sw_stats.n_frames % ARRAY_SIZE(sw_stats.frames_stats);
   uint64_t now = os_time_get_nano(); /* ns */
   auto elapsed = now - sw_stats.last_fps_update; /* ns */
   float frametime_ms = frametime_ns / 1000000.f;

   if (sw_stats.last_present_time) {
        sw_stats.frames_stats[f_idx].stats[OVERLAY_PLOTS_frame_timing] =
            frametime_ns;
      frametime_data.push_back(frametime_ms);
      frametime_data.erase(frametime_data.begin());
   }
#ifdef __linux__
   if (throttling)
      throttling->update();
#endif
   frametime = frametime_ms;
   fps = double(1000 / frametime_ms);
   if (fpsmetrics) fpsmetrics->update(now, fps);

   if (elapsed >= params.fps_sampling_period) {
      if (!hw_update_thread)
         hw_update_thread = std::make_unique<hw_info_updater>();
      hw_update_thread->update(&params, vendorID);

      if (fpsmetrics) fpsmetrics->update_thread();

      sw_stats.fps = 1000000000.0 * sw_stats.n_frames_since_update / elapsed;

      if (params.enabled[OVERLAY_PARAM_ENABLED_time]) {
         std::time_t t = std::time(nullptr);
         std::stringstream time;
         time << std::put_time(std::localtime(&t), params.time_format.c_str());
         sw_stats.time = time.str();
      }

      if (params.autostart_log && logger && !logger->autostart_init) {
         if ((std::chrono::steady_clock::now() - HUDElements.overlay_start) > std::chrono::seconds(params.autostart_log)){
            logger->start_logging();
            logger->autostart_init = true;
         }
      }

      sw_stats.n_frames_since_update = 0;
      sw_stats.last_fps_update = now;

   }
   auto min = std::min_element(frametime_data.begin(), frametime_data.end());
   auto max = std::max_element(frametime_data.begin(), frametime_data.end());
   min_frametime = min[0];
   max_frametime = max[0];
   // double min_time = UINT64_MAX, max_time = 0;
   // for (auto& stat : sw_stats.frames_stats ){
   //    min_time = MIN2(stat.stats[0], min_time);
   //    max_time = MAX2(stat.stats[0], min_time);
   // }
   // min_frametime = min_time / sw_stats.time_dividor;
   // max_frametime = max_time / sw_stats.time_dividor;
   if (params.log_interval == 0){
      logger->try_log();
   }

   sw_stats.last_present_time = now;
   sw_stats.n_frames++;
   sw_stats.n_frames_since_update++;
}

void update_hud_info(struct swapchain_stats& sw_stats, const struct overlay_params& params, uint32_t vendorID){
   uint64_t now = os_time_get_nano(); /* ns */
   uint64_t frametime_ns = now - sw_stats.last_present_time;
   if (!params.no_display || logger->is_active())
      update_hud_info_with_frametime(sw_stats, params, vendorID, frametime_ns);
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

void overlay_new_frame(const struct overlay_params& params)
{
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,-3));
   ImGui::PushStyleVar(ImGuiStyleVar_Alpha, params.alpha);
   if (!params.enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5,5));
   }
   else {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
   }
}

void overlay_end_frame()
{
   ImGui::PopStyleVar(4);
}

void position_layer(struct swapchain_stats& data, const struct overlay_params& params, const ImVec2& window_size)
{
   unsigned width = ImGui::GetIO().DisplaySize.x;
   unsigned height = ImGui::GetIO().DisplaySize.y;
   float margin = 10.0f;
   if (params.offset_x > 0 || params.offset_y > 0 || params.enabled[OVERLAY_PARAM_ENABLED_hud_no_margin])
      margin = 0.0f;

   ImGui::SetNextWindowBgAlpha(params.background_alpha);
   ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
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
      if (params.enabled[OVERLAY_PARAM_ENABLED_horizontal] && !params.enabled[OVERLAY_PARAM_ENABLED_horizontal_stretch]) {
         float content_width = (params.table_columns  * 64);
         data.main_window_pos = ImVec2((width / 2) - (window_size.x / 2) - content_width, margin + params.offset_y);
      }
      else
         data.main_window_pos = ImVec2((width / 2) - (window_size.x / 2), margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_BOTTOM_CENTER:
      if (params.enabled[OVERLAY_PARAM_ENABLED_horizontal] && !params.enabled[OVERLAY_PARAM_ENABLED_horizontal_stretch]) {
         float content_width = (params.table_columns  * 64);
         data.main_window_pos = ImVec2((width / 2) - (window_size.x / 2) - content_width,  height - window_size.y - margin + params.offset_y);
      }
      else
         data.main_window_pos = ImVec2((width / 2) - (window_size.x / 2), height - window_size.y - margin + params.offset_y);
      ImGui::SetNextWindowPos(data.main_window_pos, ImGuiCond_Always);
      break;
   case LAYER_POSITION_COUNT:
      break;
   }
}

void RenderOutlinedText(const char* text, ImU32 textColor) {
   ImGuiWindow* window = ImGui::GetCurrentWindow();
   ImGuiContext& g = *GImGui;
   const ImGuiStyle& style = g.Style;

   float outlineThickness = HUDElements.params->text_outline_thickness;
   ImVec2 textSize = ImGui::CalcTextSize(text);
   ImU32 outlineColor = ImGui::ColorConvertFloat4ToU32(HUDElements.colors.text_outline);
   ImVec2 pos = window->DC.CursorPos;

   ImDrawList* drawList = ImGui::GetWindowDrawList();

   if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_text_outline] && outlineThickness > 0.0f) {
      drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x - outlineThickness, pos.y), outlineColor, text);
      drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + outlineThickness, pos.y), outlineColor, text);
      drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x, pos.y - outlineThickness), outlineColor, text);
      drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x, pos.y + outlineThickness), outlineColor, text);
   }

   drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, textColor, text);

   ImGui::ItemSize(textSize, style.FramePadding.y);
}

void right_aligned_text(ImVec4& col, float off_x, const char *fmt, ...)
{
   ImVec2 pos = ImGui::GetCursorPos();
   char buffer[32] {};

   va_list args;
   va_start(args, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);

   if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
      ImVec2 sz = ImGui::CalcTextSize(buffer);
      ImGui::SetCursorPosX(pos.x + off_x - sz.x);
   }
   RenderOutlinedText(buffer, ImGui::ColorConvertFloat4ToU32(col));
   // ImGui::TextColored(col,"%s", buffer);
}

void center_text(const std::string& text)
{
   ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2 )- (ImGui::CalcTextSize(text.c_str()).x / 2));
}

static float get_ticker_limited_pos(float pos, float tw, float& left_limit, float& right_limit)
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
void render_mpris_metadata(const struct overlay_params& params, mutexed_metadata& meta, uint64_t frame_timing)
{
   static const float overflow = 50.f /* 3333ms * 0.5 / 16.6667 / 2 (to edge and back) */;

   if (meta.meta.valid) {
      auto color = ImGui::ColorConvertU32ToFloat4(params.media_player_color);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,0));
      ImGui::Dummy(ImVec2(0.0f, 20.0f));
      //ImGui::PushFont(data.font1);

      if (meta.ticker.needs_recalc) {
         meta.ticker.formatted.clear();
         meta.ticker.longest = 0;
         for (const auto& f : params.media_player_format)
         {
            std::string str;
            try
            {
               str = fmt::format(f,
                                   fmt::arg("artist", meta.meta.artists),
                                   fmt::arg("title", meta.meta.title),
                                   fmt::arg("album", meta.meta.album));
            }
            catch (const fmt::format_error& err)
            {
               SPDLOG_ERROR("formatting error in '{}': {}", f, err.what());
            }
            float w = ImGui::CalcTextSize(str.c_str()).x;
            meta.ticker.longest = std::max(meta.ticker.longest, w);
            meta.ticker.formatted.push_back({str, w});
         }
         meta.ticker.needs_recalc = false;
      }

      float new_pos, left_limit = 0, right_limit = 0;
      get_ticker_limited_pos(meta.ticker.pos, meta.ticker.longest, left_limit, right_limit);

      if (meta.ticker.pos < left_limit - overflow * .5f) {
         meta.ticker.dir = -1;
         meta.ticker.pos = (left_limit - overflow * .5f) + 1.f /* random */;
      } else if (meta.ticker.pos > right_limit + overflow) {
         meta.ticker.dir = 1;
         meta.ticker.pos = (right_limit + overflow) - 1.f /* random */;
      }

      meta.ticker.pos -= .5f * (frame_timing / 16666666.7f /* ns */) * meta.ticker.dir;

      for (const auto& fmt : meta.ticker.formatted)
      {
         if (fmt.text.empty()) continue;
         new_pos = get_ticker_limited_pos(meta.ticker.pos, fmt.width, left_limit, right_limit);
         ImGui::SetCursorPosX(new_pos);
         HUDElements.TextColored(color, "%s", fmt.text.c_str());
      }

      if (!meta.meta.playing) {
         HUDElements.TextColored(color, "(paused)");
      }

      //ImGui::PopFont();
      ImGui::PopStyleVar();
   }
}
#endif

static void render_benchmark(swapchain_stats& data, const struct overlay_params& params, const ImVec2& window_size, unsigned height, Clock::time_point now){
   // TODO, FIX LOG_DURATION FOR BENCHMARK
   int benchHeight = (2 + benchmark.percentile_data.size()) * real_font_size.x + 10.0f + 58;
   ImGui::SetNextWindowSize(ImVec2(window_size.x, benchHeight), ImGuiCond_Always);
   if (height - (window_size.y + data.main_window_pos.y + 5) < benchHeight)
      ImGui::SetNextWindowPos(ImVec2(data.main_window_pos.x, data.main_window_pos.y - benchHeight - 5), ImGuiCond_Always);
   else
      ImGui::SetNextWindowPos(ImVec2(data.main_window_pos.x, data.main_window_pos.y + window_size.y + 5), ImGuiCond_Always);
#ifdef MANGOAPP
   ImGui::SetNextWindowPos(ImVec2(data.main_window_pos.x, data.main_window_pos.y + window_size.y + 5), ImGuiCond_Always);
#endif
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

   float max = benchmark.fps_data.empty() ? 0.0f : *max_element(benchmark.fps_data.begin(), benchmark.fps_data.end());
   ImVec4 plotColor = HUDElements.colors.frametime;
   plotColor.w = alpha / params.background_alpha;
   ImGui::PushStyleColor(ImGuiCol_PlotLines, plotColor);
   ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0, 0.0, 0.0, alpha / params.background_alpha));
   ImGui::Dummy(ImVec2(0.0f, 8.0f));
   if (params.enabled[OVERLAY_PARAM_ENABLED_histogram])
      ImGui::PlotHistogram("", benchmark.fps_data.data(), benchmark.fps_data.size(), 0, "", 0.0f, max + 10, ImVec2(ImGui::GetContentRegionAvail().x, 50));
   else
      ImGui::PlotLines("", benchmark.fps_data.data(), benchmark.fps_data.size(), 0, "", 0.0f, max + 10, ImVec2(ImGui::GetContentRegionAvail().x, 50));
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

void horizontal_separator(struct overlay_params& params) {
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 startPos(cursorPos.x - 5, cursorPos.y + 2);
    ImVec2 endPos(startPos.x, cursorPos.y + params.font_size * 0.85);

    float outlineThickness = 1.0f;

   if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_text_outline]){
      // Draw the black outline
      drawList->AddLine(ImVec2(startPos.x - outlineThickness, startPos.y), ImVec2(startPos.x - outlineThickness, endPos.y), IM_COL32_BLACK, outlineThickness + 2);
      drawList->AddLine(ImVec2(startPos.x + outlineThickness, startPos.y), ImVec2(startPos.x + outlineThickness, endPos.y), IM_COL32_BLACK, outlineThickness + 2);
      drawList->AddLine(ImVec2(startPos.x - outlineThickness, startPos.y - outlineThickness/2), ImVec2(startPos.x + outlineThickness, startPos.y - outlineThickness/2), IM_COL32_BLACK, outlineThickness + 2);
      drawList->AddLine(ImVec2(startPos.x - outlineThickness, endPos.y + outlineThickness/2), ImVec2(startPos.x + outlineThickness, endPos.y + outlineThickness/2), IM_COL32_BLACK, outlineThickness + 2);
   } else {
      outlineThickness *= 2;
   }

    // Draw the separator line
    drawList->AddLine(startPos, endPos, params.vram_color, outlineThickness);

    ImGui::SameLine();
    ImGui::Spacing();
}

void render_imgui(swapchain_stats& data, struct overlay_params& params, ImVec2& window_size, bool is_vulkan)
{
   // data.engine = EngineTypes::GAMESCOPE;
   HUDElements.sw_stats = &data; HUDElements.params = &params;
   HUDElements.is_vulkan = is_vulkan;
   ImGui::GetIO().FontGlobalScale = params.font_scale;
   static float ralign_width = 0, old_scale = 0;
   auto io = ImGui::GetIO();
   if (params.enabled[OVERLAY_PARAM_ENABLED_fps_only]){
      window_size = ImVec2((to_string(int(HUDElements.sw_stats->fps)).length() * ImGui::CalcTextSize("A").x) + 15.f, params.height);
   } else if (params.enabled[OVERLAY_PARAM_ENABLED_horizontal]) {
      window_size = ImVec2(io.DisplaySize.x, params.height);
   } else {
      window_size = ImVec2(params.width, params.height);
   }
   unsigned height = io.DisplaySize.y;
   auto now = Clock::now();

   if (old_scale != params.font_scale) {
      HUDElements.ralign_width = ralign_width = ImGui::CalcTextSize("A").x * 4 /* characters */;
      old_scale = params.font_scale;
   }
   ImGuiTableFlags table_flags = ImGuiTableFlags_NoClip;
   if(params.enabled[OVERLAY_PARAM_ENABLED_horizontal])
      table_flags = ImGuiTableFlags_NoClip | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

   if (!params.no_display && !steam_focused && params.table_columns){
      ImGui::Begin("Main", &gui_open, ImGuiWindowFlags_NoDecoration);
      if (ImGui::BeginTable("hud", params.table_columns, table_flags )) {
         HUDElements.place = 0;
         for (auto& func : HUDElements.ordered_functions){
            if(!params.enabled[OVERLAY_PARAM_ENABLED_horizontal] && func.name != "exec")
               ImGui::TableNextRow();
            func.run();
            HUDElements.place += 1;
            if(!HUDElements.ordered_functions.empty() && params.enabled[OVERLAY_PARAM_ENABLED_horizontal] && HUDElements.ordered_functions.size() != (size_t)HUDElements.place)
               horizontal_separator(params);
         }

         if (params.enabled[OVERLAY_PARAM_ENABLED_horizontal]) {
            if (HUDElements.table_columns_count > 0 && HUDElements.table_columns_count < 65 )
               params.table_columns = HUDElements.table_columns_count;
            if(!params.enabled[OVERLAY_PARAM_ENABLED_horizontal_stretch]) {
               float content_width = ImGui::GetContentRegionAvail().x - (params.table_columns * 64);
               window_size = ImVec2(content_width, params.height);
            }
         }
         ImGui::EndTable();
         HUDElements.table_columns_count = 0;
      }

      if(logger->is_active())
         ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(data.main_window_pos.x + window_size.x - 15, data.main_window_pos.y + 15), 10, params.engine_color, 20);
      window_size = ImVec2(window_size.x, ImGui::GetCursorPosY() + 10.0f);
      ImGui::End();
      if((now - logger->last_log_end()) < 12s && !logger->is_active())
         render_benchmark(data, params, window_size, height, now);
   }

   if(params.enabled[OVERLAY_PARAM_ENABLED_fcat])
     {
       fcatstatus.update(&params);
       auto window_corners = fcatstatus.get_overlay_corners();
       auto p_min=window_corners[0];
       auto p_max=window_corners[1];
       auto window_size= window_corners[2];
       ImGui::SetNextWindowPos(p_min, ImGuiCond_Always);
       ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
       ImGui::SetNextWindowSize(window_size);
       ImGui::Begin("FCAT", &fcat_open, ImGuiWindowFlags_NoDecoration| ImGuiWindowFlags_NoBackground);
       ImGui::GetWindowDrawList()->AddRectFilled(p_min,p_max,fcatstatus.get_next_color(data),0.0);
       ImGui::End();
       ImGui::PopStyleVar();
     }
}

void init_cpu_stats(overlay_params& params)
{
#ifdef __linux__
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

void init_gpu_stats(uint32_t& vendorID, uint32_t reported_deviceID, overlay_params& params)
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

#ifdef __linux__
   // NVIDIA
   if (vendorID == 0x10de)
      if(checkNvidia(pci_dev))
         vendorID = 0x10de;

   string path;
   string drm = "/sys/class/drm/";

   if (vendorID==0x8086){
      auto dirs = ls(drm.c_str(), "card");
      for (auto& dir : dirs) {
         if (dir.find("-") != std::string::npos)
             continue; // filter display adapters

         FILE *fp;
         string device = path + "/device/device";
         if ((fp = fopen(device.c_str(), "r"))){
            uint32_t temp = 0;
            if (fscanf(fp, "%x", &temp) == 1) {
               if (temp != reported_deviceID){
                  fclose(fp);
                  SPDLOG_DEBUG("DeviceID does not match vulkan report {:X}", reported_deviceID);
                  continue;
               }
               deviceID = temp;
            }
            fclose(fp);
         }

         string vendor = path + "/device/vendor";
         if ((fp = fopen(vendor.c_str(), "r"))){
            uint32_t temp = 0;
            if (fscanf(fp, "%x", &temp) != 1 || temp != 0x8086) {
               fclose(fp);
               continue;
            }
            fclose(fp);
         }
         path = drm + dir;
         drm_dev = dir;
         SPDLOG_DEBUG("Intel: using drm device {}", drm_dev);
         intel = std::make_unique<Intel>();
         break;
      }
   }

   if (vendorID == 0x5143) {
      auto dirs = ls(drm.c_str(), "card");
      for (auto& dir : dirs) {
         if (dir.find("-") != std::string::npos) {
             continue; // filter display adapters
         }
         path = drm + dir;
         drm_dev = dir;
         SPDLOG_DEBUG("msm: using drm device {}", drm_dev);
         msm = std::make_unique<MSM>();
      }
   }

   if (vendorID == 0x1002
       || gpu.find("Radeon") != std::string::npos
       || gpu.find("AMD") != std::string::npos) {
      string path;
      string drm = "/sys/class/drm/";

      auto dirs = ls(drm.c_str(), "card");
      for (auto& dir : dirs) {
         if (dir.find("-") != std::string::npos) {
             continue; // filter display adapters
         }
         path = drm + dir;

         SPDLOG_DEBUG("drm path check: {}", path);
         if (pci_bus_parsed && pci_dev) {
            string pci_device = read_symlink((path + "/device").c_str());
            SPDLOG_DEBUG("PCI device symlink: '{}'", pci_device);
            if (!ends_with(pci_device, pci_dev)) {
               SPDLOG_DEBUG("skipping GPU, no PCI ID match");
               continue;
            }
         }

         FILE *fp;
         string device = path + "/device/device";
         if ((fp = fopen(device.c_str(), "r"))){
            uint32_t temp = 0;
            if (fscanf(fp, "%x", &temp) == 1) {
               if (!pci_bus_parsed && reported_deviceID && temp != reported_deviceID){
                  fclose(fp);
                  SPDLOG_DEBUG("DeviceID does not match vulkan report {:X}", reported_deviceID);
                  continue;
               }
               deviceID = temp;
            }
            fclose(fp);
         }

         string vendor = path + "/device/vendor";
         if ((fp = fopen(vendor.c_str(), "r"))){
            uint32_t temp = 0;
            if (fscanf(fp, "%x", &temp) != 1 || temp != 0x1002) {
               fclose(fp);
               continue;
            }
            fclose(fp);
         }

         const std::string device_path = path + "/device";
         const std::string gpu_metrics_path = device_path + "/gpu_metrics";
         if (amdgpu_verify_metrics(gpu_metrics_path)) {
            gpu_metrics_exists = true;
            metrics_path = gpu_metrics_path;
            throttling = std::make_unique<Throttling>();
            SPDLOG_DEBUG("Using gpu_metrics of {}", gpu_metrics_path);
         }

         if (!amdgpu.vram_total)
            amdgpu.vram_total = fopen((device_path + "/mem_info_vram_total").c_str(), "r");
         if (!amdgpu.vram_used)
            amdgpu.vram_used = fopen((device_path + "/mem_info_vram_used").c_str(), "r");
         if (!amdgpu.gtt_used)
            amdgpu.gtt_used = fopen((device_path + "/mem_info_gtt_used").c_str(), "r");

         const std::string hwmon_path = device_path + "/hwmon/";
         if (fs::exists(hwmon_path)){
            const auto dirs = ls(hwmon_path.c_str(), "hwmon", LS_DIRS);
            for (const auto& dir : dirs) {
               if (!amdgpu.temp)
                  amdgpu.temp = fopen((hwmon_path + dir + "/temp1_input").c_str(), "r");
               if (!amdgpu.junction_temp)
                  amdgpu.junction_temp = fopen((hwmon_path + dir + "/temp2_input").c_str(), "r");
               if (!amdgpu.memory_temp)
                  amdgpu.memory_temp = fopen((hwmon_path + dir + "/temp3_input").c_str(), "r");
               if (!amdgpu.core_clock)
                  amdgpu.core_clock = fopen((hwmon_path + dir + "/freq1_input").c_str(), "r");
               if (!amdgpu.gpu_voltage_soc)
                  amdgpu.gpu_voltage_soc = fopen((hwmon_path + dir + "/in0_input").c_str(), "r");
            }

            if (!metrics_path.empty())
               break;

            // The card output nodes - cardX-output, will point to the card node
            // As such the actual metrics nodes will be missing.
            amdgpu.busy = fopen((device_path + "/gpu_busy_percent").c_str(), "r");
            if (!amdgpu.busy)
               continue;

            SPDLOG_DEBUG("using amdgpu path: {}", device_path);

            for (const auto& dir : dirs) {
               if (!amdgpu.memory_clock)
                  amdgpu.memory_clock = fopen((hwmon_path + dir + "/freq2_input").c_str(), "r");
               if (!amdgpu.power_usage)
                  amdgpu.power_usage = fopen((hwmon_path + dir + "/power1_average").c_str(), "r");
               if (!amdgpu.power_usage)
                  amdgpu.power_usage = fopen((hwmon_path + dir + "/power1_input").c_str(), "r");
               if (!amdgpu.fan)
                  amdgpu.fan = fopen((hwmon_path + dir + "/fan1_input").c_str(), "r");
            }
         }
         break;
      }

      // don't bother then
      if (metrics_path.empty() && !amdgpu.busy && vendorID != 0x8086) {
         params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
      }
   }
#endif
   if (!params.permit_upload)
      SPDLOG_DEBUG("Uploading is disabled (permit_upload = 0)");
}

void init_system_info(){
   #ifdef __linux__
      const char* ld_preload = getenv("LD_PRELOAD");
      if (ld_preload)
         unsetenv("LD_PRELOAD");

      ram =  exec("sed -n 's/^MemTotal: *\\([0-9]*\\).*/\\1/p' /proc/meminfo");
      trim(ram);
      cpu =  exec("sed -n 's/^model name.*: \\(.*\\)/\\1/p' /proc/cpuinfo | sed 's/([^)]*)//g' | tail -n1");
      trim(cpu);
      kernel = exec("uname -r");
      trim(kernel);
      os = exec("sed -n 's/PRETTY_NAME=\\(.*\\)/\\1/p' /etc/*-release");
      os.erase(remove(os.begin(), os.end(), '\"' ), os.end());
      trim(os);
      cpusched = read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

      const char* mangohud_recursion = getenv("MANGOHUD_RECURSION");
      if (!mangohud_recursion) {
         setenv("MANGOHUD_RECURSION", "1", 1);
         // driver = exec("glxinfo -B | sed -n 's/^OpenGL version.*: \\(.*\\)/\\1/p' | sed 's/([^)]*)//g;s/  / /g'");
         // trim(driver);
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
            if (preloader == "wine-preloader")
               findVersion << "\"" << dir << "/wine\" --version";
            else
               findVersion << "\"" << dir << "/wine64\" --version";
            const char *wine_env = getenv("WINELOADERNOEXEC");
            if (wine_env)
               unsetenv("WINELOADERNOEXEC");
            wineVersion = exec(findVersion.str());
            trim(wineVersion);
            SPDLOG_DEBUG("WINE version: {}", wineVersion);
            if (wine_env)
               setenv("WINELOADERNOEXEC", wine_env, 1);
         }
      }
      else {
           wineVersion = "";
      }
      // check for gamemode and vkbasalt
      fs::path path("/proc/self/map_files/");
      for (auto& p : fs::directory_iterator(path)) {
         auto filename = p.path().string();
         auto sym = read_symlink(filename.c_str());
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
      SPDLOG_DEBUG("Driver:{}", driver);
      SPDLOG_DEBUG("CPU Scheduler:{}", cpusched);
#endif
}

void update_fan(){
   // This just handles steam deck fan for now
   static bool init;
   string hwmon_path;

   if (!init){
      string path = "/sys/class/hwmon/";
      auto dirs = ls(path.c_str(), "hwmon", LS_DIRS);
      for (auto& dir : dirs) {
         string full_path = (path + dir + "/name").c_str();
         if (read_line(full_path).find("steamdeck_hwmon") != string::npos){
            hwmon_path = path + dir + "/fan1_input";
            break;
         }
      }
   }

   if (!hwmon_path.empty())
      fan_speed = stoi(read_line(hwmon_path));
   else
      fan_speed = -1;
}

void next_hud_position(struct overlay_params& params){
   if (params.position < (overlay_param_position::LAYER_POSITION_COUNT - 1)){
      params.position = static_cast<overlay_param_position>(params.position + 1);
   } else {
      params.position = static_cast<overlay_param_position>(0);
   }
}
