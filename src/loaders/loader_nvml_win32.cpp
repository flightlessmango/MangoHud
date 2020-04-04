// This is generated file. Do not modify directly.
// Path to the code generator: /home/crz/git/MangoHud/generate_library_loader.py .

#include <windows.h>
#include "loader_nvml.h"
#include <stdio.h>

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_NVML_H_DLOPEN) && !defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
#error neither LIBRARY_LOADER_NVML_H_DLOPEN nor LIBRARY_LOADER_NVML_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_NVML_H_DLOPEN) && defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
#error both LIBRARY_LOADER_NVML_H_DLOPEN and LIBRARY_LOADER_NVML_H_DT_NEEDED defined
#endif

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
  library_ = LoadLibrary(library_name.c_str());
  if (!library_)
    return false;
#endif


#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  nvmlInit_v2 =
      reinterpret_cast<decltype(this->nvmlInit_v2)>(
          GetProcAddress((HMODULE)library_, "nvmlInit_v2"));
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
          GetProcAddress((HMODULE)library_, "nvmlShutdown"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetUtilizationRates"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetTemperature"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetPciInfo_v3"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetCount_v2"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetHandleByIndex_v2"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetHandleByPciBusId_v2"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetMemoryInfo"));
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
          GetProcAddress((HMODULE)library_, "nvmlDeviceGetClockInfo"));
#endif
#if defined(LIBRARY_LOADER_NVML_H_DT_NEEDED)
  nvmlDeviceGetClockInfo = &::nvmlDeviceGetClockInfo;
#endif
  if (!nvmlDeviceGetClockInfo) {
    CleanUp(true);
    return false;
  }

  loaded_ = true;
  return true;
}

void libnvml_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  if (unload) {
    FreeLibrary((HMODULE)library_);
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

}
