#include <sstream>
#include <iomanip>
#include <ctime>
#include "overlay.h"
#include "logging.h"
#include "cpu.h"
#include "gpu.h"
#include "memory.h"
#include "timing.hpp"
#include "mesa/util/macros.h"

void update_hw_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID)
{
   if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_stats] || logger->is_active()) {
      cpuStats.UpdateCPUData();

      if (params.enabled[OVERLAY_PARAM_ENABLED_core_load])
         cpuStats.UpdateCoreMhz();
      if (params.enabled[OVERLAY_PARAM_ENABLED_cpu_temp] || logger->is_active())
         cpuStats.UpdateCpuTemp();
   }

   if (params.enabled[OVERLAY_PARAM_ENABLED_gpu_stats] || logger->is_active()) {
      if (vendorID == 0x1002)
         getAmdGpuInfo();

      if (vendorID == 0x10de)
         getNvidiaGpuInfo();
   }

   // get ram usage/max
   if (params.enabled[OVERLAY_PARAM_ENABLED_ram] || logger->is_active())
      update_meminfo();
   if (params.enabled[OVERLAY_PARAM_ENABLED_io_read] || params.enabled[OVERLAY_PARAM_ENABLED_io_write])
      getIoStats(&sw_stats.io);

   currentLogData.gpu_load = gpu_info.load;
   currentLogData.gpu_temp = gpu_info.temp;
   currentLogData.gpu_core_clock = gpu_info.CoreClock;
   currentLogData.gpu_mem_clock = gpu_info.MemClock;
   currentLogData.gpu_vram_used = gpu_info.memoryUsed;
   currentLogData.ram_used = memused;

   currentLogData.cpu_load = cpuStats.GetCPUDataTotal().percent;
   currentLogData.cpu_temp = cpuStats.GetCPUDataTotal().temp;

   logger->notify_data_valid();
}

void update_hud_info(struct swapchain_stats& sw_stats, struct overlay_params& params, uint32_t vendorID){
   if(not logger) logger = std::make_unique<Logger>(&params);
   uint32_t f_idx = sw_stats.n_frames % ARRAY_SIZE(sw_stats.frames_stats);
   uint64_t now = os_time_get(); /* us */

   double elapsed = (double)(now - sw_stats.last_fps_update); /* us */
   fps = 1000000.0f * sw_stats.n_frames_since_update / elapsed;
   if (logger->is_active())
      benchmark.fps_data.push_back(fps);

   if (sw_stats.last_present_time) {
        sw_stats.frames_stats[f_idx].stats[OVERLAY_PLOTS_frame_timing] =
            now - sw_stats.last_present_time;
   }

   frametime = now - sw_stats.last_present_time;
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