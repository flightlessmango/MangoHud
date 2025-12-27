#include <spdlog/spdlog.h>
#include "zenpower.hpp"

Zenpower::Zenpower() {
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("zenpower");

    if (hwmon.base_dir.empty())
        return;

    hwmon.setup(sensors);

    for (const hwmon_sensor& s : sensors) {
        if (!hwmon.is_open(s.generic_name)) {
            SPDLOG_DEBUG("Failed to open \"{}\".", s.generic_name);
            return;
        }
    }

    _is_initialized = true;
}

void Zenpower::pre_poll_overrides() {
    hwmon.poll_sensors();
}

float Zenpower::get_power_usage() {
    uint64_t core_power = hwmon.get_sensor_value("core_power");
    uint64_t soc_power  = hwmon.get_sensor_value("soc_power");
    
    float usage = (core_power + soc_power) / 1'000'000.f;
    usage /= std::chrono::duration_cast<std::chrono::milliseconds>(delta_time_ns).count();
    usage *= 1000.f;

    return usage;
}
