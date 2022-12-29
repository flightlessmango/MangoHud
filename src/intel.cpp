#include <thread>
#include "overlay.h"
#include "gpu.h"
#include "spdlog/spdlog.h"
#include <nlohmann/json.hpp>
#include <sys/stat.h>
using json = nlohmann::json;

static bool init_intel = false;
struct gpuInfo gpu_info_intel {};
static char drm_dev[10];

static void intelGpuThread(){
    init_intel = true;
    static char stdout_buffer[4096];

    std::string cmd("mango_intel_stats ");
    cmd += drm_dev;
    FILE* mango_intel_stats = popen(cmd.c_str() , "r");
    while (fgets(stdout_buffer, sizeof(stdout_buffer), mango_intel_stats)) {
        if ( stdout_buffer[0] != '{' || stdout_buffer[strlen(stdout_buffer)-1] != '\n') {
            SPDLOG_ERROR("Overran 4k buffer for fgets output. Expect sadness:\n {}", stdout_buffer);
            continue;
        }

        json j = json::parse(stdout_buffer);
        if  (j.contains("gpu_busy_pct"))
            gpu_info_intel.load = j["gpu_busy_pct"].get<int>();
        if  (j.contains("gpu_clock_mhz"))
            gpu_info_intel.CoreClock = j["gpu_clock_mhz"].get<int>();
        if  (j.contains("igpu_power_w"))
            gpu_info_intel.powerUsage = j["igpu_power_w"].get<float>();
        if  (j.contains("dgpu_power_w"))
            gpu_info_intel.powerUsage = j["dgpu_power_w"].get<float>();
        if  (j.contains("cpu_power_w"))
            gpu_info_intel.apu_cpu_power = j["cpu_power_w"].get<float>();
    }

    int exitcode = pclose(mango_intel_stats) / 256;
    if (exitcode > 0){
        if (exitcode == 127)
            SPDLOG_INFO("Failed to open '{}'", "mango_intel_stats");

        if (exitcode == 1)
            SPDLOG_INFO("Missing permissions for '{}'", "mango_intel_stats");

        SPDLOG_INFO("Disabling gpu_stats");
        _params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
    }
}

void getIntelGpuInfo(const char *_drm_dev){
    if (!init_intel) {
        assert(strlen(_drm_dev) < sizeof(drm_dev)-1);
        strcpy(drm_dev,  _drm_dev);
        std::thread(intelGpuThread).detach();
    }

    gpu_info = gpu_info_intel;
}
