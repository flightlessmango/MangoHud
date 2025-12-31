#include <spdlog/spdlog.h>
#include "zenergy.hpp"

Zenergy::Zenergy() {
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("zenergy");

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

void Zenergy::pre_poll_overrides() {
    hwmon.poll_sensors();
}

float Zenergy::get_power_usage() {
    uint64_t total_usage = hwmon.get_sensor_value("energy");

    if (previous_usage == 0) {
        previous_usage = total_usage;
        return 0.f;
    }
    
    float usage = total_usage - previous_usage;
    usage /= std::chrono::duration_cast<std::chrono::milliseconds>(delta_time_ns).count();

    previous_usage = total_usage;

    return usage / 1'000.f;
}
