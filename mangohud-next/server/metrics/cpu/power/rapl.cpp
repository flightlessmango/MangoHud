#include <limits>
#include <spdlog/spdlog.h>
#include "rapl.hpp"

RAPL::RAPL() {
    hwmon.base_dir = rapl_path;
    hwmon.setup(sensors);

    if (!hwmon.is_open("energy")) {
        SPDLOG_WARN("Failed to open \"{}\".", rapl_path);
        return;
    }

    _is_initialized = true;
}

void RAPL::pre_poll_overrides() {
    hwmon.poll_sensors();
}

float RAPL::get_power_usage() {
    uint64_t total_usage = hwmon.get_sensor_value("energy");

    if (previous_usage == 0 || total_usage == previous_usage) {
        previous_usage = total_usage;
        return 0.f;
    }
    
    float usage = total_usage - previous_usage;
    usage /= std::chrono::duration_cast<std::chrono::milliseconds>(delta_time_ns).count();
    usage /= 1'000.f;

    previous_usage = total_usage;

    if (usage == std::numeric_limits<float>::infinity())
        return 0.f;

    return usage;
}
