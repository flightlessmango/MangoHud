#include "panfrost.hpp"
#include <cmath>

Panfrost::Panfrost(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-panfrost"), FDInfo(drm_node) {
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("gpu_thermal");
    hwmon.setup(sensors, drm_node);
}

void Panfrost::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
}

int Panfrost::get_temperature() {
    return std::round(hwmon.get_sensor_value("temp") / 1000.f);
}

int Panfrost::get_core_clock() {
    if (fdinfo.pids.empty())
        return 0;

    // frequency is the same across all pids, so just take first pid
    FDInfoBase& data = fdinfo.pids.begin()->second;
    std::vector<fdinfo_data>& fds_data = data.fds_data;

    if (fds_data.empty())
        return 0;

    std::string freq_str = fds_data[0]["drm-curfreq-fragment"];

    if (freq_str.empty())
        return 0;

    float freq = std::stoull(freq_str) / 1'000'000;

    return std::round(freq);
}

int Panfrost::get_process_load(pid_t pid) {
    uint64_t* previous_gpu_time = &previous_gpu_times[pid];

    uint64_t fragment_time_now = fdinfo.get_gpu_time(pid, "drm-engine-fragment");
    uint64_t vertex_time_now   = fdinfo.get_gpu_time(pid, "drm-engine-vertex-tiler");

    uint64_t gpu_time_now = fragment_time_now + vertex_time_now;

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

float Panfrost::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-resident-memory");
}
