#include "gpu.h"
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
#include <spdlog/spdlog.h>
#include "nvctrl.h"
#include "timing.hpp"
#include "file_utils.h"
#ifdef HAVE_NVML
#include "nvidia_info.h"
#endif

#include "amdgpu.h"

using namespace std::chrono_literals;

std::shared_ptr<gpu_device> g_active_gpu;
std::unordered_map<std::string /*device*/, std::shared_ptr<gpu_device>> g_gpu_devices;

bool NVCtrlInfo::init()
{
#ifdef HAVE_XNVCTRL
    // FIXME correct device index
    return checkXNVCtrl();
#else
    return false;
#endif
}

void NVCtrlInfo::update(const struct overlay_params& params)
{
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        info.load = nvctrl_info.load;
        info.temp = nvctrl_info.temp;
        info.memory_used = nvctrl_info.memoryUsed;
        info.core_clock = nvctrl_info.CoreClock;
        info.memory_clock = nvctrl_info.MemClock;
        info.power_usage = 0;
        info.memory_total = nvctrl_info.memoryTotal;
        return;
    }
#endif
}

void getAmdGpuInfo(amdgpu_files& amdgpu, gpu_info& gpu_info, bool has_metrics){
    int64_t value = 0;
    if (!has_metrics){
        if (amdgpu.busy) {
            rewind(amdgpu.busy);
            fflush(amdgpu.busy);
            int value = 0;
            if (fscanf(amdgpu.busy, "%d", &value) != 1)
                value = 0;
            gpu_info.load = value;
        }

        if (amdgpu.core_clock) {
            rewind(amdgpu.core_clock);
            fflush(amdgpu.core_clock);
            if (fscanf(amdgpu.core_clock, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info.core_clock = value / 1000000;
        }

        if (amdgpu.memory_clock) {
            rewind(amdgpu.memory_clock);
            fflush(amdgpu.memory_clock);
            if (fscanf(amdgpu.memory_clock, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info.memory_clock = value / 1000000;
        }

        if (amdgpu.power_usage) {
            rewind(amdgpu.power_usage);
            fflush(amdgpu.power_usage);
            if (fscanf(amdgpu.power_usage, "%" PRId64, &value) != 1)
                value = 0;

            gpu_info.power_usage = value / 1000000;
        }
    }

    if (amdgpu.vram_total) {
        rewind(amdgpu.vram_total);
        fflush(amdgpu.vram_total);
        if (fscanf(amdgpu.vram_total, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memory_total = value;
    }

    if (amdgpu.vram_used) {
        rewind(amdgpu.vram_used);
        fflush(amdgpu.vram_used);
        if (fscanf(amdgpu.vram_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memory_used = value;
    }
    // On some GPUs SMU can sometimes return the wrong temperature.
    // As HWMON is way more visible than the SMU metrics, let's always trust it as it is the most likely to work
    if (amdgpu.temp){
        rewind(amdgpu.temp);
        fflush(amdgpu.temp);
        int value = 0;
        if (fscanf(amdgpu.temp, "%d", &value) != 1)
            value = 0;
        gpu_info.temp = value / 1000;
    }

    if (amdgpu.gtt_used) {
        rewind(amdgpu.gtt_used);
        fflush(amdgpu.gtt_used);
        if (fscanf(amdgpu.gtt_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.gtt_used = value;
    }
}

bool AMDGPUHWMonInfo::init()
{
    const auto device_path  = sysfs_path + "/device";
    const auto hwmon_path = device_path + "/hwmon/";
    files.busy = fopen((device_path + "/gpu_busy_percent").c_str(), "r");
    files.vram_total = fopen((device_path + "/mem_info_vram_total").c_str(), "r");
    files.vram_used = fopen((device_path + "/mem_info_vram_used").c_str(), "r");
    files.gtt_used = fopen((device_path + "/mem_info_gtt_used").c_str(), "r");

    const auto dirs = ls(hwmon_path, "hwmon", LS_DIRS);
    for (const auto& dir : dirs) {
        if (!files.core_clock)
            files.core_clock = fopen((hwmon_path + dir + "/freq1_input").c_str(), "r");
        if (!files.memory_clock)
            files.memory_clock = fopen((hwmon_path + dir + "/freq2_input").c_str(), "r");
        if (!files.temp)
            files.temp = fopen((hwmon_path + dir + "/temp1_input").c_str(), "r");
        if (!files.power_usage)
            files.power_usage = fopen((hwmon_path + dir + "/power1_average").c_str(), "r");
    }

    return files.busy && files.temp && files.vram_total && files.vram_used;
}

void AMDGPUHWMonInfo::update(const struct overlay_params& params)
{
    getAmdGpuInfo(files, info, false);
}
