#pragma once

#define NVML_NO_UNVERSIONED_FUNC_DEFS

#include <string>
#include <memory>
#include <atomic>
#include "nvml.h"

class libnvml_loader {
public:
    libnvml_loader();
    ~libnvml_loader();

    bool load();
    bool is_loaded() { return loaded_; }

    decltype(&::nvmlInit_v2) nvmlInit_v2;
    decltype(&::nvmlShutdown) nvmlShutdown;
    decltype(&::nvmlDeviceGetUtilizationRates) nvmlDeviceGetUtilizationRates;
    decltype(&::nvmlDeviceGetTemperature) nvmlDeviceGetTemperature;
    decltype(&::nvmlDeviceGetPciInfo_v3) nvmlDeviceGetPciInfo_v3;
    decltype(&::nvmlDeviceGetCount_v2) nvmlDeviceGetCount_v2;
    decltype(&::nvmlDeviceGetHandleByIndex_v2) nvmlDeviceGetHandleByIndex_v2;
    decltype(&::nvmlDeviceGetHandleByPciBusId_v2) nvmlDeviceGetHandleByPciBusId_v2;
    decltype(&::nvmlDeviceGetMemoryInfo) nvmlDeviceGetMemoryInfo;
    decltype(&::nvmlDeviceGetClockInfo) nvmlDeviceGetClockInfo;
    decltype(&::nvmlErrorString) nvmlErrorString;
    decltype(&::nvmlDeviceGetPowerUsage) nvmlDeviceGetPowerUsage;
    decltype(&::nvmlDeviceGetPowerManagementLimit) nvmlDeviceGetPowerManagementLimit;
    decltype(&::nvmlDeviceGetCurrentClocksThrottleReasons) nvmlDeviceGetCurrentClocksThrottleReasons;
    decltype(&::nvmlUnitGetFanSpeedInfo) nvmlUnitGetFanSpeedInfo;
    decltype(&::nvmlUnitGetHandleByIndex) nvmlUnitGetHandleByIndex;
    decltype(&::nvmlDeviceGetFanSpeed) nvmlDeviceGetFanSpeed;
    decltype(&::nvmlDeviceGetGraphicsRunningProcesses) nvmlDeviceGetGraphicsRunningProcesses;

private:
    void unload();
    void* library_ = nullptr;

    const std::string library_name = "libnvidia-ml.so.1";
    std::atomic<bool> loaded_ = false;

    // Disallow copy constructor and assignment operator.
    libnvml_loader(const libnvml_loader&);
    void operator=(const libnvml_loader&);
};

std::shared_ptr<libnvml_loader> get_libnvml_loader();
