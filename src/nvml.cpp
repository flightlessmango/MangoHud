#include <spdlog/spdlog.h>
#include "loaders/loader_nvml.h"
#include "nvidia_info.h"
#include <iostream>
#include "overlay.h"
#include "overlay_params.h"
#include "gpu.h"

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
nvmlPciInfo_t nvidiaPciInfo;
bool nvmlSuccess = false;
unsigned int nvidiaTemp = 0, nvidiaCoreClock = 0, nvidiaMemClock = 0, nvidiaPowerUsage = 0;
unsigned long long nvml_throttle_reasons;
struct nvmlUtilization_st nvidiaUtilization;
struct nvmlMemory_st nvidiaMemory {};

static std::unique_ptr<libnvml_loader, std::function<void(libnvml_loader*)>> nvml_shutdown;

bool checkNVML()
{
    auto& nvml = get_libnvml_loader();
    if (!nvml.IsLoaded())
    {
        SPDLOG_ERROR("Failed to load NVML");
        return false;
    }

    if (nvmlSuccess)
        return nvmlSuccess;

    result = nvml.nvmlInit();
    if (NVML_SUCCESS != result)
    {
        SPDLOG_ERROR("Nvidia module not loaded");
        return false;
    }

    nvml_shutdown = { &nvml,
        [](libnvml_loader *nvml) -> void {
            nvml->nvmlShutdown();
        }
    };
    nvmlSuccess = true;
    return nvmlSuccess;
}

bool getNVMLInfo(nvmlDevice_t device, gpu_info& gpu_info, const struct overlay_params& params){
    nvmlReturn_t response;
    unsigned long long nvml_throttle_reasons = 0;
    unsigned int nvidiaTemp, nvidiaCoreClock, nvidiaMemClock, nvidiaPowerUsage;
    struct nvmlUtilization_st nvidiaUtilization;
    struct nvmlMemory_st nvidiaMemory;

    auto& nvml = get_libnvml_loader();
    response = nvml.nvmlDeviceGetUtilizationRates(device, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(device, &nvidiaMemory);
    nvml.nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &nvidiaCoreClock);
    nvml.nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &nvidiaMemClock);
    nvml.nvmlDeviceGetPowerUsage(device, &nvidiaPowerUsage);
    if (params.enabled[OVERLAY_PARAM_ENABLED_throttling_status]){
        nvml.nvmlDeviceGetCurrentClocksThrottleReasons(device, &nvml_throttle_reasons);
        gpu_info.is_temp_throttled = (nvml_throttle_reasons & 0x0000000000000060LL) != 0;
        gpu_info.is_power_throttled = (nvml_throttle_reasons & 0x000000000000008CLL) != 0;
        gpu_info.is_other_throttled = (nvml_throttle_reasons & 0x0000000000000112LL) != 0;
    }

    gpu_info.load = nvidiaUtilization.gpu;
    gpu_info.temp = nvidiaTemp;
    gpu_info.memory_used = nvidiaMemory.used;
    gpu_info.core_clock = nvidiaCoreClock;
    gpu_info.memory_clock = nvidiaMemClock;
    gpu_info.power_usage = nvidiaPowerUsage / 1000;
    gpu_info.memory_total = nvidiaMemory.total;

    if (response == NVML_ERROR_NOT_SUPPORTED) {
        if (nvmlSuccess)
            SPDLOG_ERROR("nvmlDeviceGetUtilizationRates failed");
        nvmlSuccess = false;
    }
    return nvmlSuccess;
}

bool NVMLInfo::init()
{
    nvmlDevice_t nvml_dev;
    if (!checkNVML())
        return false;

    auto& nvml = get_libnvml_loader();
    nvmlReturn_t ret = NVML_ERROR_UNKNOWN;
    if ((ret = nvml.nvmlDeviceGetHandleByPciBusId(pci_device.c_str(), &nvml_dev)) != NVML_SUCCESS)
    {
        SPDLOG_ERROR("Getting device handle by PCI bus ID failed: {}", nvml.nvmlErrorString(ret));
    }

    if (ret != NVML_SUCCESS)
    {
        unsigned int deviceCount = 0;
        ret = nvml.nvmlDeviceGetCount(&deviceCount);

        if (ret == NVML_SUCCESS)
        {
            for (unsigned i = 0; i < deviceCount; i++)
            {
                ret = nvml.nvmlDeviceGetHandleByIndex(0, &nvml_dev);
                if (ret != NVML_SUCCESS)
                    SPDLOG_ERROR("Getting device {} handle failed: {}", i, nvml.nvmlErrorString(ret));
                else if (nvml.nvmlDeviceGetPciInfo_v3(nvml_dev, &nvidiaPciInfo) == NVML_SUCCESS)
                {
                    if (this->deviceID == nvidiaPciInfo.pciDeviceId >> 16)
                        break;
                }
            }
        }
    }

    device = reinterpret_cast<gpu_handles*>(nvml_dev);
    return true;
}

void NVMLInfo::update(const struct overlay_params& params)
{
    if (nvmlSuccess){
        getNVMLInfo(reinterpret_cast<nvmlDevice_t>(device), info, params);
        return;
    }
}
