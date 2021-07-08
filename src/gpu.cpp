#include "gpu.h"
#include <inttypes.h>
#include "nvctrl.h"
#ifdef HAVE_NVML
#include "nvidia_info.h"
#endif

std::unordered_map<int32_t, struct gpuInfo> gpu_info;
std::unordered_map<int32_t, amdgpu_files> amdgpu= {};
bool checkNvidia(const char *pci_dev){
    bool nvSuccess = false;
#ifdef HAVE_NVML
    nvSuccess = checkNVML(pci_dev) && getNVMLInfo();
#endif
#ifdef HAVE_XNVCTRL
    if (!nvSuccess)
        nvSuccess = checkXNVCtrl();
#endif
#ifdef _WIN32
    if (!nvSuccess)
        nvSuccess = checkNVAPI();
#endif
    return nvSuccess;
}

void getNvidiaGpuInfo(int32_t deviceID){
#ifdef HAVE_NVML
    if (nvmlSuccess){
        getNVMLInfo();
        gpu_info[deviceID].load = nvidiaUtilization.gpu;
        gpu_info[deviceID].temp = nvidiaTemp;
        gpu_info[deviceID].memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info[deviceID].CoreClock = nvidiaCoreClock;
        gpu_info[deviceID].MemClock = nvidiaMemClock;
        gpu_info[deviceID].powerUsage = nvidiaPowerUsage / 1000;
        gpu_info[deviceID].memoryTotal = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        return;
    }
#endif
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        gpu_info[deviceID].load = nvctrl_info.load;
        gpu_info[deviceID].temp = nvctrl_info.temp;
        gpu_info[deviceID].memoryUsed = nvctrl_info.memoryUsed / (1024.f);
        gpu_info[deviceID].CoreClock = nvctrl_info.CoreClock;
        gpu_info[deviceID].MemClock = nvctrl_info.MemClock;
        gpu_info[deviceID].powerUsage = 0;
        gpu_info[deviceID].memoryTotal = nvctrl_info.memoryTotal;
        return;
    }
#endif
#ifdef _WIN32
nvapi_util();
#endif
}

void getAmdGpuInfo(int32_t deviceID){
  if (amdgpu[deviceID].busy) {
      rewind(amdgpu[deviceID].busy);
      fflush(amdgpu[deviceID].busy);
      int value = 0;
      if (fscanf(amdgpu[deviceID].busy, "%d", &value) != 1)
          value = 0;
      gpu_info[deviceID].load = value;
  }

  if (amdgpu[deviceID].temp) {
      rewind(amdgpu[deviceID].temp);
      fflush(amdgpu[deviceID].temp);
      int value = 0;
      if (fscanf(amdgpu[deviceID].temp, "%d", &value) != 1)
          value = 0;
      gpu_info[deviceID].temp = value / 1000;
  }

  int64_t value = 0;

  if (amdgpu[deviceID].vram_total) {
      rewind(amdgpu[deviceID].vram_total);
      fflush(amdgpu[deviceID].vram_total);
      if (fscanf(amdgpu[deviceID].vram_total, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info[deviceID].memoryTotal = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu[deviceID].vram_used) {
        rewind(amdgpu[deviceID].vram_used);
        fflush(amdgpu[deviceID].vram_used);
        if (fscanf(amdgpu[deviceID].vram_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info[deviceID].memoryUsed = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu[deviceID].core_clock) {
        rewind(amdgpu[deviceID].core_clock);
        fflush(amdgpu[deviceID].core_clock);
        if (fscanf(amdgpu[deviceID].core_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].CoreClock = value / 1000000;
    }

    if (amdgpu[deviceID].memory_clock) {
        rewind(amdgpu[deviceID].memory_clock);
        fflush(amdgpu[deviceID].memory_clock);
        if (fscanf(amdgpu[deviceID].memory_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].MemClock = value / 1000000;
    }

    if (amdgpu[deviceID].power_usage) {
        rewind(amdgpu[deviceID].power_usage);
        fflush(amdgpu[deviceID].power_usage);
        if (fscanf(amdgpu[deviceID].power_usage, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].powerUsage = value / 1000000;
    }
}
