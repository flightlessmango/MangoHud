#include <spdlog/spdlog.h>
#include "loaders/loader_nvml.h"
#include "nvidia_info.h"
#include <iostream>
#include "overlay.h"
#include "overlay_params.h"

nvmlReturn_t result;
nvmlDevice_t nvidiaDevice;
nvmlPciInfo_t nvidiaPciInfo;
bool nvmlSuccess = false;
unsigned int nvidiaTemp = 0, nvidiaCoreClock = 0, nvidiaMemClock = 0, nvidiaPowerUsage = 0, nvidiaFanSpeed = 0;
unsigned long long nvml_throttle_reasons;
struct nvmlUtilization_st nvidiaUtilization;
struct nvmlMemory_st nvidiaMemory {};
struct nvmlUnitFanSpeeds_st nvidiaFanSpeeds {};
struct nvmlUnit_st* nvidiaUnit {};

bool checkNVML(const char* pciBusId){
    auto& nvml = get_libnvml_loader();
    if (nvml.IsLoaded()){
        result = nvml.nvmlInit();
        if (NVML_SUCCESS != result) {
            SPDLOG_ERROR("Nvidia module not loaded");
        } else {
            nvmlReturn_t ret = NVML_ERROR_UNKNOWN;
            if (pciBusId && ((ret = nvml.nvmlDeviceGetHandleByPciBusId(pciBusId, &nvidiaDevice)) != NVML_SUCCESS)) {
                SPDLOG_ERROR("Getting device handle by PCI bus ID failed: {}", nvml.nvmlErrorString(ret));
                SPDLOG_ERROR("Using index 0.");
            }

            if (ret != NVML_SUCCESS)
                ret = nvml.nvmlDeviceGetHandleByIndex(0, &nvidiaDevice);

            if (ret != NVML_SUCCESS)
                SPDLOG_ERROR("Getting device handle failed: {}", nvml.nvmlErrorString(ret));

            nvmlSuccess = (ret == NVML_SUCCESS);
            if (ret == NVML_SUCCESS)
                nvml.nvmlDeviceGetPciInfo_v3(nvidiaDevice, &nvidiaPciInfo);

            return nvmlSuccess;
        }
    } else {
        SPDLOG_ERROR("Failed to load NVML");
    }

    return false;
}

bool getNVMLInfo(const struct overlay_params& params){
    nvmlReturn_t response;
    auto& nvml = get_libnvml_loader();
    response = nvml.nvmlDeviceGetUtilizationRates(nvidiaDevice, &nvidiaUtilization);
    nvml.nvmlDeviceGetTemperature(nvidiaDevice, NVML_TEMPERATURE_GPU, &nvidiaTemp);
    nvml.nvmlDeviceGetMemoryInfo(nvidiaDevice, &nvidiaMemory);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_GRAPHICS, &nvidiaCoreClock);
    nvml.nvmlDeviceGetClockInfo(nvidiaDevice, NVML_CLOCK_MEM, &nvidiaMemClock);
    nvml.nvmlDeviceGetPowerUsage(nvidiaDevice, &nvidiaPowerUsage);
    nvml.nvmlUnitGetHandleByIndex(0, &nvidiaUnit);
    nvml.nvmlUnitGetFanSpeedInfo(nvidiaUnit, &nvidiaFanSpeeds);
    nvidiaFanSpeed = nvidiaFanSpeeds.fans[0].speed;
    deviceID = nvidiaPciInfo.pciDeviceId >> 16;
    if (params.enabled[OVERLAY_PARAM_ENABLED_throttling_status])
        nvml.nvmlDeviceGetCurrentClocksThrottleReasons(nvidiaDevice, &nvml_throttle_reasons);

    if (response == NVML_ERROR_NOT_SUPPORTED) {
        if (nvmlSuccess)
            SPDLOG_ERROR("nvmlDeviceGetUtilizationRates failed");
        nvmlSuccess = false;
    }
    return nvmlSuccess;
}
