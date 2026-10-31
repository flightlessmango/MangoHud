#include "nvidia.hpp"

Nvidia::Nvidia(
    const std::string &drm_node, const std::string &pci_dev,
    uint16_t vendor_id, uint16_t device_id)
    :
    GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-nvidia"
) {
    nvml_available = init_nvml(pci_dev);
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
    return slow_metrics.vram_used;
}

float Nvidia::get_memory_total() {
    return slow_metrics.memory_total;
}

int Nvidia::get_memory_clock() {
    return slow_metrics.memory_clock;
}

int Nvidia::get_temperature() {
    return slow_metrics.temperature;
}

int Nvidia::get_core_clock() {
    return slow_metrics.core_clock;
}

float Nvidia::get_power_usage() {
    return slow_metrics.power_usage;
}

float Nvidia::get_power_limit() {
    return slow_metrics.power_limit;
}

bool Nvidia::get_is_power_throttled() {
    return (throttle_reasons & 0x000000000000008CLL) != 0;
}

bool Nvidia::get_is_temp_throttled() {
    return (throttle_reasons & 0x0000000000000060LL) != 0;
}

bool Nvidia::get_is_other_throttled() {
    return (throttle_reasons & 0x0000000000000112LL) != 0;
}

int Nvidia::get_fan_speed() {
    return slow_metrics.fan_speed;
}

bool Nvidia::get_fan_rpm() {
    return false;
}

float Nvidia::get_process_vram_used(pid_t pid) {
    float used_memory = 0.f;

    for (const nvmlProcessInfo_v1_t& proc : processes) {
        if (static_cast<pid_t>(proc.pid) != pid)
            continue;

        used_memory = proc.usedGpuMemory / 1024.f / 1024.f / 1024.f;
        break;
    }

    return used_memory;
}

void Nvidia::pre_poll_overrides() {
    throttle_reasons = 0;
    nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &throttle_reasons);

    const auto now = std::chrono::steady_clock::now();
    if (last_slow_poll != std::chrono::steady_clock::time_point{} &&
        now - last_slow_poll < gpu_metrics_update_period)
        return;

    last_slow_poll = now;
    slow_metrics = {};
    nvmlMemory_st memory = {};
    if (nvml->nvmlDeviceGetMemoryInfo(device, &memory) == NVML_SUCCESS) {
        slow_metrics.vram_used = memory.used / 1024.f / 1024.f / 1024.f;
        slow_metrics.memory_total = memory.total / 1024.f / 1024.f / 1024.f;
    }

    unsigned int value = 0;
    if (nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &value) == NVML_SUCCESS)
        slow_metrics.memory_clock = value;
    if (nvml->nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &value) == NVML_SUCCESS)
        slow_metrics.temperature = value;
    if (nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &value) == NVML_SUCCESS)
        slow_metrics.core_clock = value;
    if (nvml->nvmlDeviceGetPowerUsage(device, &value) == NVML_SUCCESS)
        slow_metrics.power_usage = value / 1000.f;
    if (nvml->nvmlDeviceGetPowerManagementLimit(device, &value) == NVML_SUCCESS)
        slow_metrics.power_limit = value / 1000.f;
    if (nvml->nvmlDeviceGetFanSpeed(device, &value) == NVML_SUCCESS)
        slow_metrics.fan_speed = value;

    processes = get_processes();
}
