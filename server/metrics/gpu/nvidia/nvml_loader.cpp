#include "nvml_loader.hpp"
#include <dlfcn.h>
#include <spdlog/spdlog.h>

static std::shared_ptr<libnvml_loader> libnvml_;

std::shared_ptr<libnvml_loader> get_libnvml_loader()
{
    if (!libnvml_)
        libnvml_ = std::make_shared<libnvml_loader>();

    return libnvml_;
}

libnvml_loader::libnvml_loader() {
    load();
}

libnvml_loader::~libnvml_loader() {
    unload();
}

#define LOAD_NVML_FUNCTION(name) \
    do {                    \
        name = reinterpret_cast<decltype(this->name)>(dlsym(library_, #name)); \
        if (!name) { \
            unload(); \
            return false; \
        } \
    } while(0)

bool libnvml_loader::load() {
    if (loaded_)
        return true;

    library_ = dlopen(library_name.c_str(), RTLD_LAZY | RTLD_NODELETE);

    if (!library_) {
        SPDLOG_ERROR("Failed to open {}: {}", library_name, dlerror());
        return false;
    }

    LOAD_NVML_FUNCTION(nvmlInit_v2);
    LOAD_NVML_FUNCTION(nvmlShutdown);
    LOAD_NVML_FUNCTION(nvmlDeviceGetUtilizationRates);
    LOAD_NVML_FUNCTION(nvmlDeviceGetTemperature);
    LOAD_NVML_FUNCTION(nvmlDeviceGetPciInfo_v3);
    LOAD_NVML_FUNCTION(nvmlDeviceGetCount_v2);
    LOAD_NVML_FUNCTION(nvmlDeviceGetHandleByIndex_v2);
    LOAD_NVML_FUNCTION(nvmlDeviceGetHandleByPciBusId_v2);
    LOAD_NVML_FUNCTION(nvmlDeviceGetMemoryInfo);
    LOAD_NVML_FUNCTION(nvmlDeviceGetClockInfo);
    LOAD_NVML_FUNCTION(nvmlErrorString);
    LOAD_NVML_FUNCTION(nvmlDeviceGetPowerUsage);
    LOAD_NVML_FUNCTION(nvmlDeviceGetPowerManagementLimit);
    LOAD_NVML_FUNCTION(nvmlDeviceGetCurrentClocksThrottleReasons);
    LOAD_NVML_FUNCTION(nvmlUnitGetFanSpeedInfo);
    LOAD_NVML_FUNCTION(nvmlUnitGetHandleByIndex);
    LOAD_NVML_FUNCTION(nvmlDeviceGetFanSpeed);
    LOAD_NVML_FUNCTION(nvmlDeviceGetGraphicsRunningProcesses);

    loaded_ = true;
    return true;
}

#undef LOAD_NVML_FUNCTION

void libnvml_loader::unload() {
    if (library_) {
        dlclose(library_);
        library_ = NULL;
    }

    loaded_ = false;

    nvmlInit_v2 = nullptr;
    nvmlShutdown = nullptr;
    nvmlDeviceGetUtilizationRates = nullptr;
    nvmlDeviceGetTemperature = nullptr;
    nvmlDeviceGetPciInfo_v3 = nullptr;
    nvmlDeviceGetCount_v2 = nullptr;
    nvmlDeviceGetHandleByIndex_v2 = nullptr;
    nvmlDeviceGetHandleByPciBusId_v2 = nullptr;
    nvmlDeviceGetMemoryInfo = nullptr;
    nvmlDeviceGetClockInfo = nullptr;
    nvmlErrorString = nullptr;
    nvmlDeviceGetPowerUsage = nullptr;
    nvmlDeviceGetPowerManagementLimit = nullptr;
    nvmlDeviceGetCurrentClocksThrottleReasons = nullptr;
    nvmlUnitGetFanSpeedInfo = nullptr;
    nvmlUnitGetHandleByIndex = nullptr;
    nvmlDeviceGetFanSpeed = nullptr;
    nvmlDeviceGetGraphicsRunningProcesses = nullptr;
}
