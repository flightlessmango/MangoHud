// This is generated file. Do not modify directly.
// Path to the code generator: /home/crz/git/MangoHud/generate_library_loader.py .

#ifndef LIBRARY_LOADER_NVML_H
#define LIBRARY_LOADER_NVML_H

#include "nvml.h"
#define LIBRARY_LOADER_NVML_H_DLOPEN

#include <string>
#include <dlfcn.h>

class libnvml_loader {
 public:
  libnvml_loader();
  libnvml_loader(const std::string& library_name) : libnvml_loader() {
    Load(library_name);
  }
  ~libnvml_loader();

  bool Load(const std::string& library_name);
  bool IsLoaded() { return loaded_; }

  decltype(&::nvmlInit_v2) nvmlInit_v2;
  decltype(&::nvmlShutdown) nvmlShutdown;
  decltype(&::nvmlDeviceGetUtilizationRates) nvmlDeviceGetUtilizationRates;
  decltype(&::nvmlDeviceGetTemperature) nvmlDeviceGetTemperature;
  decltype(&::nvmlDeviceGetPciInfo_v3) nvmlDeviceGetPciInfo_v3;
  decltype(&::nvmlDeviceGetCount_v2) nvmlDeviceGetCount_v2;
  decltype(&::nvmlDeviceGetHandleByIndex_v2) nvmlDeviceGetHandleByIndex_v2;
  decltype(&::nvmlDeviceGetHandleByPciBusId_v2) nvmlDeviceGetHandleByPciBusId_v2;
  decltype(&::nvmlDeviceGetMemoryInfo) nvmlDeviceGetMemoryInfo;

 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_NVML_H_DLOPEN)
  void* library_ = nullptr;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libnvml_loader(const libnvml_loader&);
  void operator=(const libnvml_loader&);
};

#endif  // LIBRARY_LOADER_NVML_H
