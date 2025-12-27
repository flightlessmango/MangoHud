#include "dpu.hpp"
#include <cmath>

MSM_DPU::MSM_DPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-msm-dpu"), FDInfo(drm_node) {
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("gpu");
    hwmon.setup(sensors, drm_node);
}

void MSM_DPU::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
}

int MSM_DPU::get_temperature() {
    return static_cast<int>(::lroundf(hwmon.get_sensor_value("temp") / 1000.0f));
}

int MSM_DPU::get_process_load(pid_t pid) {
    uint64_t* previous_gpu_time = &previous_gpu_times[pid];

    uint64_t gpu_time_now = fdinfo.get_gpu_time(pid, "drm-engine-gpu");

    if (!*previous_gpu_time) {
        *previous_gpu_time = gpu_time_now;
        return 0;
    }

    float delta_gpu_time = gpu_time_now - *previous_gpu_time;
    float result = delta_gpu_time / delta_time_ns.count() * 100;

    if (result > 100.f)
        result = 100.f;

    *previous_gpu_time = gpu_time_now;

    return static_cast<int>(::lroundf(result));
}
