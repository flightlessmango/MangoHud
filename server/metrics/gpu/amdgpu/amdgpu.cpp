#include "amdgpu.hpp"
#include <cmath>

AMDGPU::AMDGPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-amdgpu"),
    FDInfo(drm_node), AMDGPUMetrics(drm_node)
{
    hwmon.setup(sensors, drm_node);

    sysfs_hwmon.base_dir = "/sys/class/drm/" + drm_node + "/device";
    sysfs_hwmon.setup(sysfs_sensors);

    metrics_available = gpu_metrics.setup();
}

void AMDGPU::pre_poll_overrides() {
    hwmon.poll_sensors();
    sysfs_hwmon.poll_sensors();
    fdinfo.poll_all();

    if (metrics_available)
        gpu_metrics.poll();
}

int AMDGPU::get_load() {
    if (metrics_available) {
        // some GPUs report load as centipercent
        if (gpu_metrics.metrics.gpu_load_percent > 100)
            return gpu_metrics.metrics.gpu_load_percent / 100;

        return gpu_metrics.metrics.gpu_load_percent;
    } else
        return sysfs_hwmon.get_sensor_value("load");
}

float AMDGPU::get_vram_used() {
    float used = sysfs_hwmon.get_sensor_value("vram_used") / 1024.f / 1024.f / 1024.f;
    return used;
}

float AMDGPU::get_gtt_used() {
    float used = sysfs_hwmon.get_sensor_value("gtt_used") / 1024.f / 1024.f / 1024.f;
    return used;
}

float AMDGPU::get_memory_total() {
    float used = sysfs_hwmon.get_sensor_value("vram_total") / 1024.f / 1024.f / 1024.f;
    return used;
}

int AMDGPU::get_memory_clock() {
    if (metrics_available)
        return gpu_metrics.metrics.current_uclk_mhz;
    else
        return hwmon.get_sensor_value("memory_clock") / 1'000'000.f;
}

int AMDGPU::get_memory_temp() {
    return hwmon.get_sensor_value("memory_temp");
}

int AMDGPU::get_temperature() {
    if (metrics_available) {
        if (gpu_metrics.is_apu())
            return gpu_metrics.metrics.apu_cpu_temp_c;
        else
            return gpu_metrics.metrics.gpu_temp_c;
    } else {
        float temp = hwmon.get_sensor_value("temperature") / 1'000.f;
        return std::round(temp);
    }
}

int AMDGPU::get_junction_temperature() {
    return hwmon.get_sensor_value("junction_temp");
}

int AMDGPU::get_core_clock() {
    // If we are on VANGOGH (Steam Deck), then
    // always use core clock from GPU metrics.
    if (metrics_available && (device_id == 0x1435 || device_id == 0x163f))
        return gpu_metrics.metrics.current_gfxclk_mhz;
    else
        return hwmon.get_sensor_value("frequency") / 1'000'000.f;
}

int AMDGPU::get_voltage() {
    return hwmon.get_sensor_value("voltage");
}

float AMDGPU::get_power_usage() {
    // It's set to -1 if it's not available in gpu_metrics
    if (metrics_available && gpu_metrics.metrics.average_gfx_power_w != -1.0f)
        return gpu_metrics.metrics.average_gfx_power_w;
    else {
        if (hwmon.is_open("average_power"))
            return hwmon.get_sensor_value("average_power") / 1'000'000.f;
        else
            return hwmon.get_sensor_value("current_power") / 1'000'000.f;
    }
}

float AMDGPU::get_power_limit() {
    return hwmon.get_sensor_value("power_limit");
}

bool AMDGPU::get_is_apu() {
    if (metrics_available)
        return gpu_metrics.is_apu();

    return false;
}

float AMDGPU::get_apu_cpu_power() {
    if (metrics_available)
        return gpu_metrics.metrics.average_cpu_power_w;
    else
        return 0.f;
}

int AMDGPU::get_apu_cpu_temp() {
    if (metrics_available)
        return gpu_metrics.metrics.apu_cpu_temp_c;
    else
        return 0.f;
}

bool AMDGPU::get_is_power_throttled() {
    if (metrics_available)
        return gpu_metrics.metrics.is_power_throttled;
    else
        return false;
}

bool AMDGPU::get_is_current_throttled() {
    if (metrics_available)
        return gpu_metrics.metrics.is_current_throttled;
    else
        return false;
}

bool AMDGPU::get_is_temp_throttled() {
    if (metrics_available)
        return gpu_metrics.metrics.is_temp_throttled;
    else
        return false;
}

bool AMDGPU::get_is_other_throttled() {
    if (metrics_available)
        return gpu_metrics.metrics.is_other_throttled;
    else
        return false;
}

int AMDGPU::get_fan_speed() {
    if (metrics_available)
        return gpu_metrics.metrics.fan_speed;
    else
        return hwmon.get_sensor_value("fan");
}

int AMDGPU::get_process_load(pid_t pid) {
    uint64_t* previous_gpu_time = &previous_gpu_times[pid];
    uint64_t gpu_time_now = fdinfo.get_gpu_time(pid, "drm-engine-gfx");

    if (!*previous_gpu_time) {
        *previous_gpu_time = gpu_time_now;
        return 0;
    }

    float delta_gpu_time = gpu_time_now - *previous_gpu_time;
    float result = delta_gpu_time / delta_time_ns.count() * 100;

    if (result > 100.f)
        result = 100.f;

    *previous_gpu_time = gpu_time_now;

    return std::round(result);
}

float AMDGPU::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-memory-vram");
}

float AMDGPU::get_process_gtt_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-memory-gtt");
}
