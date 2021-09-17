#include <spdlog/spdlog.h>
#include "loaders/loader_nvml.h"
#include "nvidia_info.h"
#include <iostream>
#include "overlay.h"

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
nvmlPciInfo_t nvidiaPciInfo;
bool nvmlSuccess = false;
unsigned int nvidiaTemp = 0, nvidiaCoreClock = 0, nvidiaMemClock = 0, nvidiaPowerUsage = 0;
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

bool getNVMLInfo(nvmlDevice_t device){
    nvmlReturn_t response;
    auto& nvml = get_libnvml_loader();
    response = nvml.nvmlDeviceGetUtilizationRates(device, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(device, &nvidiaMemory);
    nvml.nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &nvidiaCoreClock);
    nvml.nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &nvidiaMemClock);
    nvml.nvmlDeviceGetPowerUsage(device, &nvidiaPowerUsage);

    if (response == NVML_ERROR_NOT_SUPPORTED)
        nvmlSuccess = false;
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

void NVMLInfo::update()
{
    if (nvmlSuccess){
        getNVMLInfo(reinterpret_cast<nvmlDevice_t>(device));
        s.load = nvidiaUtilization.gpu;
        s.temp = nvidiaTemp;
        s.memory_used = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        s.core_clock = nvidiaCoreClock;
        s.memory_clock = nvidiaMemClock;
        s.power_usage = nvidiaPowerUsage / 1000;
        s.memory_total = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        return;
    }
}
