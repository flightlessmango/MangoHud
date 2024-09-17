// This is generated file. Do not modify directly.
// Path to the code generator: /home/crz/git/MangoHud/generate_library_loader.py .

#include "loader_nvml.h"
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_NVML_H_DLOPEN) && !defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
#error neither LIBRARY_LOADER_NVML_H_DLOPEN nor LIBRARY_LOADER_NVML_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_NVML_H_DLOPEN) && defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
#error both LIBRARY_LOADER_NVML_H_DLOPEN and LIBRARY_LOADER_NVML_H_DT_NEEDED defined
#endif

static std::unique_ptr<libnvml_loader> libnvml_;

libnvml_loader& get_libnvml_loader()
{
    if (!libnvml_)
        libnvml_ = std::make_unique<libnvml_loader>("libnvidia-ml.so.1");
    return *libnvml_.get();
}

libnvml_loader::libnvml_loader() : loaded_(false) {
}

libnvml_loader::~libnvml_loader() {
  CleanUp(loaded_);
}

bool libnvml_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  library_ = dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_) {
    SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " {}: {}", library_name, dlerror());
    return false;
  }
#endif


#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlInit_v2 =
      reinterpret_cast<decltype(this->nvmlInit_v2)>(
          dlsym(library_, "nvmlInit_v2"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlInit_v2 = &::nvmlInit_v2;
#endif
  if (!nvmlInit_v2) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlShutdown =
      reinterpret_cast<decltype(this->nvmlShutdown)>(
          dlsym(library_, "nvmlShutdown"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlShutdown = &::nvmlShutdown;
#endif
  if (!nvmlShutdown) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetUtilizationRates =
      reinterpret_cast<decltype(this->nvmlDeviceGetUtilizationRates)>(
          dlsym(library_, "nvmlDeviceGetUtilizationRates"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetUtilizationRates = &::nvmlDeviceGetUtilizationRates;
#endif
  if (!nvmlDeviceGetUtilizationRates) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetTemperature =
      reinterpret_cast<decltype(this->nvmlDeviceGetTemperature)>(
          dlsym(library_, "nvmlDeviceGetTemperature"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetTemperature = &::nvmlDeviceGetTemperature;
#endif
  if (!nvmlDeviceGetTemperature) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetPciInfo_v3 =
      reinterpret_cast<decltype(this->nvmlDeviceGetPciInfo_v3)>(
          dlsym(library_, "nvmlDeviceGetPciInfo_v3"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetPciInfo_v3 = &::nvmlDeviceGetPciInfo_v3;
#endif
  if (!nvmlDeviceGetPciInfo_v3) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetCount_v2 =
      reinterpret_cast<decltype(this->nvmlDeviceGetCount_v2)>(
          dlsym(library_, "nvmlDeviceGetCount_v2"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetCount_v2 = &::nvmlDeviceGetCount_v2;
#endif
  if (!nvmlDeviceGetCount_v2) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetHandleByIndex_v2 =
      reinterpret_cast<decltype(this->nvmlDeviceGetHandleByIndex_v2)>(
          dlsym(library_, "nvmlDeviceGetHandleByIndex_v2"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetHandleByIndex_v2 = &::nvmlDeviceGetHandleByIndex_v2;
#endif
  if (!nvmlDeviceGetHandleByIndex_v2) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetHandleByPciBusId_v2 =
      reinterpret_cast<decltype(this->nvmlDeviceGetHandleByPciBusId_v2)>(
          dlsym(library_, "nvmlDeviceGetHandleByPciBusId_v2"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetHandleByPciBusId_v2 = &::nvmlDeviceGetHandleByPciBusId_v2;
#endif
  if (!nvmlDeviceGetHandleByPciBusId_v2) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetMemoryInfo =
      reinterpret_cast<decltype(this->nvmlDeviceGetMemoryInfo)>(
          dlsym(library_, "nvmlDeviceGetMemoryInfo"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetMemoryInfo = &::nvmlDeviceGetMemoryInfo;
#endif
  if (!nvmlDeviceGetMemoryInfo) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetClockInfo =
      reinterpret_cast<decltype(this->nvmlDeviceGetClockInfo)>(
          dlsym(library_, "nvmlDeviceGetClockInfo"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetClockInfo = &::nvmlDeviceGetClockInfo;
#endif
  if (!nvmlDeviceGetClockInfo) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlErrorString =
      reinterpret_cast<decltype(this->nvmlErrorString)>(
          dlsym(library_, "nvmlErrorString"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlErrorString = &::nvmlErrorString;
#endif
  if (!nvmlErrorString) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetCurrentClocksThrottleReasons =
      reinterpret_cast<decltype(this->nvmlDeviceGetCurrentClocksThrottleReasons)>(
          dlsym(library_, "nvmlDeviceGetCurrentClocksThrottleReasons"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetCurrentClocksThrottleReasons = &::nvmlDeviceGetCurrentClocksThrottleReasons;
#endif
  if (!nvmlErrorString) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetPowerUsage =
      reinterpret_cast<decltype(this->nvmlDeviceGetPowerUsage)>(
          dlsym(library_, "nvmlDeviceGetPowerUsage"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetPowerUsage = &::nvmlDeviceGetPowerUsage;
#endif
  if (!nvmlDeviceGetPowerUsage) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlUnitGetFanSpeedInfo =
      reinterpret_cast<decltype(this->nvmlUnitGetFanSpeedInfo)>(
          dlsym(library_, "nvmlUnitGetFanSpeedInfo"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlUnitGetFanSpeedInfo = &::nvmlUnitGetFanSpeedInfo;
#endif
  if (!nvmlUnitGetFanSpeedInfo) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlUnitGetHandleByIndex =
      reinterpret_cast<decltype(this->nvmlUnitGetHandleByIndex)>(
          dlsym(library_, "nvmlUnitGetHandleByIndex"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlUnitGetHandleByIndex = &::nvmlUnitGetHandleByIndex;
#endif
  if (!nvmlUnitGetHandleByIndex) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetFanSpeed =
      reinterpret_cast<decltype(this->nvmlDeviceGetFanSpeed)>(
          dlsym(library_, "nvmlDeviceGetFanSpeed"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetFanSpeed = &::nvmlDeviceGetFanSpeed;
#endif
  if (!nvmlDeviceGetFanSpeed) {
    CleanUp(true);
    return false;
  }

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlDeviceGetComputeRunningProcesses =
      reinterpret_cast<decltype(this->nvmlDeviceGetComputeRunningProcesses)>(
          dlsym(library_, "nvmlDeviceGetComputeRunningProcesses"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetComputeRunningProcesses = &::nvmlDeviceGetComputeRunningProcesses;
#endif
  if (!nvmlDeviceGetComputeRunningProcesses) {
    CleanUp(true);
    return false;
  }

  loaded_ = true;
  return true;
}


void libnvml_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }
#endif
  loaded_ = false;
  nvmlInit_v2 = NULL;
  nvmlShutdown = NULL;
  nvmlDeviceGetUtilizationRates = NULL;
  nvmlDeviceGetTemperature = NULL;
  nvmlDeviceGetPciInfo_v3 = NULL;
  nvmlDeviceGetCount_v2 = NULL;
  nvmlDeviceGetHandleByIndex_v2 = NULL;
  nvmlDeviceGetHandleByPciBusId_v2 = NULL;
  nvmlDeviceGetCurrentClocksThrottleReasons = NULL;
  nvmlUnitGetFanSpeedInfo = NULL;
  nvmlUnitGetHandleByIndex = NULL;
  nvmlDeviceGetFanSpeed = NULL;
  nvmlDeviceGetComputeRunningProcesses = NULL;
}
