#include "nvidia.hpp"

Nvidia::Nvidia(
    const std::string &drm_node, const std::string &pci_dev,
    uint16_t vendor_id, uint16_t device_id)
    :
    GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-nvidia"
) {
    nvml_available = init_nvml(pci_dev);
    nvapi_available = init_nvapi(pci_dev);
}

bool Nvidia::init_nvml(const std::string& pci_dev) {
    nvml = get_libnvml_loader();

    if (!nvml)
        return false;

    if (!nvml->is_loaded())
        return false;

    nvmlReturn_t result = nvml->nvmlInit_v2();
    
    if (NVML_SUCCESS != result) {
        SPDLOG_ERROR("Nvidia module initialization failed: {}", nvml->nvmlErrorString(result));
        return false;
    }

    result = nvml->nvmlDeviceGetHandleByPciBusId_v2(pci_dev.c_str(), &device);
    
    if (NVML_SUCCESS != result) {
        SPDLOG_ERROR("Getting device handle by PCI bus ID failed: {}", nvml->nvmlErrorString(result));
        return false;
    }

    return true;
}

bool Nvidia::init_nvapi(const std::string& pci_dev) {
    nvapi = get_libnvapi_loader();

    unsigned int pciBusId = 0;

    {
        unsigned int domain, bus, slot, func;

        if (sscanf(pci_dev.c_str(), "%x:%02x:%02x.%x", &domain, &bus, &slot, &func) != 4) {
            SPDLOG_ERROR("nvapi: Failed to parse PCI device ID: '{}'", pci_dev);
            return false;
        }

        pciBusId = bus;
    }

    if (!nvapi)
        return false;

    if (!nvapi->is_loaded())
        return false;

    int result = nvapi->nvapi_Initialize();

    if (result != 0) {
        char msg[64] = {};
        nvapi->nvapi_GetErrorMessage(result, &msg);
        SPDLOG_ERROR("nvapi_Initialize() failed: {}", msg);
        return false;
    }

    long nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS] = {};
    unsigned int numOfGPUs = 0;

    result = nvapi->nvapi_EnumPhysicalGPUs(&nvGPUHandle, &numOfGPUs);

    if (result != 0) {
        char msg[64] = {};
        nvapi->nvapi_GetErrorMessage(result, &msg);
        SPDLOG_ERROR("nvapi_EnumPhysicalGPUs() failed: {}", msg);
        return false;
    }

    for (unsigned int i = 0; i < numOfGPUs; i++) {
        unsigned int busId = 0;
        result = nvapi->nvapi_GPU_GetBusId(nvGPUHandle[i], &busId);

        if (result == 0 && busId == pciBusId) {
            nvapi_device = nvGPUHandle[i];
            break;
        }
    }

    if (nvapi_device == 0) {
        SPDLOG_ERROR("nvapi: Failed to find gpu with {}", pci_dev);
        return false;
    }

    return true;
}

const std::vector<nvmlProcessInfo_v1_t> Nvidia::get_processes() {
    unsigned int info_count = 0;

    std::vector<nvmlProcessInfo_v1_t> cur_process_info(info_count);
    nvmlReturn_t ret = nvml->nvmlDeviceGetGraphicsRunningProcesses(device, &info_count, cur_process_info.data());

    if (ret != NVML_ERROR_INSUFFICIENT_SIZE)
        return {};

    cur_process_info.resize(info_count);
    ret = nvml->nvmlDeviceGetGraphicsRunningProcesses(device, &info_count, cur_process_info.data());

    if (ret != NVML_SUCCESS)
        return {};

    return cur_process_info;
}

int Nvidia::get_load() {
    nvmlUtilization_st nvml_utilization;

    nvmlReturn_t ret = nvml->nvmlDeviceGetUtilizationRates(device, &nvml_utilization);

    if (ret != NVML_SUCCESS)
        return 0;

    return nvml_utilization.gpu;
}

float Nvidia::get_vram_used() {
    nvmlMemory_st nvml_memory;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetMemoryInfo(device, &nvml_memory);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return nvml_memory.used / 1024.f / 1024.f / 1024.f;
}

float Nvidia::get_memory_total() {
    nvmlMemory_st nvml_memory;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetMemoryInfo(device, &nvml_memory);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return nvml_memory.total / 1024.f / 1024.f / 1024.f;
}

int Nvidia::get_memory_clock() {
    uint32_t memory_clock;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &memory_clock);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return memory_clock;
}

int Nvidia::get_temperature() {
    uint32_t temperature = 0;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temperature);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return temperature;
}

int Nvidia::get_core_clock() {
    uint32_t core_clock = 0;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &core_clock);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return core_clock;
}

int Nvidia::get_voltage() {
    uint32_t voltage = 0;
    bool try_nvapi = false;

    if (nvml->nvmlInternalGetVoltage != nullptr) {
        nvmlReturn_t ret = nvml->nvmlInternalGetVoltage(device, &voltage);

        if (ret != NVML_SUCCESS) {
            try_nvapi = true;
        }
    } else {
        try_nvapi = true;
    }

    if (try_nvapi && nvapi_available) {
        libnvapi_loader::NvApiVoltage voltage_info = {};

        int nv_ret = nvapi->nvapi_GetVoltage(nvapi_device, &voltage_info);
        
        if (nv_ret == 0) {
            voltage = voltage_info.value_microvolts;
        }
    }

    return voltage / 1000.f;
}

float Nvidia::get_power_usage() {
    uint32_t power_usage = 0;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetPowerUsage(device, &power_usage);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return power_usage / 1000.f;
}

float Nvidia::get_power_limit() {
    uint32_t power_limit = 0;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetPowerManagementLimit(device, &power_limit);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return power_limit / 1000.f;
}

bool Nvidia::get_is_power_throttled() {
    unsigned long long throttle_reasons;
    nvmlReturn_t ret = nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &throttle_reasons);

    if (ret != NVML_SUCCESS)
        return 0;

    return (throttle_reasons & 0x000000000000008CLL) != 0;
}

bool Nvidia::get_is_temp_throttled() {
    unsigned long long throttle_reasons;
    nvmlReturn_t ret = nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &throttle_reasons);

    if (ret != NVML_SUCCESS)
        return 0;

    return (throttle_reasons & 0x0000000000000060LL) != 0;
}

bool Nvidia::get_is_other_throttled() {
    unsigned long long throttle_reasons;
    nvmlReturn_t ret = nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &throttle_reasons);

    if (ret != NVML_SUCCESS)
        return 0;

    return (throttle_reasons & 0x0000000000000112LL) != 0;
}

int Nvidia::get_fan_speed() {
    uint32_t fan_speed = 0;
    
    nvmlReturn_t ret = nvml->nvmlDeviceGetFanSpeed(device, &fan_speed);

    if (ret != NVML_SUCCESS)
        return 0;
    
    return fan_speed;
}

bool Nvidia::get_fan_rpm() {
    return false;
}

float Nvidia::get_process_vram_used(pid_t pid) {
    const std::vector<nvmlProcessInfo_v1_t> info = get_processes();
    float used_memory = 0.f;

    for (const nvmlProcessInfo_v1_t& proc : info) {
        if (static_cast<pid_t>(proc.pid) != pid)
            continue;

        used_memory = proc.usedGpuMemory / 1024.f / 1024.f / 1024.f;
        break;
    }

    return used_memory;
}
