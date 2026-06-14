#include "kgsl.hpp"
#include <cmath>

MSM_KGSL::MSM_KGSL(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-msm-kgsl") {
    hwmon.base_dir = "/sys/class/kgsl/kgsl-3d0";
    hwmon.setup(sensors);
}

void MSM_KGSL::pre_poll_overrides() {
    hwmon.poll_sensors();
}

int MSM_KGSL::get_load() {
    return hwmon.get_sensor_value("load");
}

int MSM_KGSL::get_temperature() {
    return std::round(hwmon.get_sensor_value("temp") / 1000.f);
}

int MSM_KGSL::get_core_clock() {
    return hwmon.get_sensor_value("core_clock");
}
