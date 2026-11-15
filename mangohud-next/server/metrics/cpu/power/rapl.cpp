#include <limits>
#include <spdlog/spdlog.h>
#include "rapl.hpp"

RAPL::RAPL() {
    std::error_code ec;
    if (!fs::is_directory(rapl_path, ec))
        return;

    hwmon.base_dir = rapl_path;
    hwmon.setup(sensors);

    if (!hwmon.is_open("energy")) {
        SPDLOG_WARN("Failed to open \"{}\".", rapl_path);
        return;
    }

    _is_initialized = true;
    find_uncore();
}

void RAPL::find_uncore() {
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(rapl_path, ec)) {
        // the domains of the package are intel-rapl:0:N
        if (entry.path().filename().string().rfind("intel-rapl:0:", 0) != 0)
            continue;

        std::ifstream ifs_name(entry.path() / "name");
        std::string name;
        std::getline(ifs_name, name);

        if (name != "uncore")
            continue;

        uncore.base_dir = entry.path().string();
        uncore.setup(sensors);

        if (!uncore.is_open("energy"))
            SPDLOG_DEBUG("Failed to open \"{}/energy_uj\".", uncore.base_dir);

        return;
    }
}

void RAPL::pre_poll_overrides() {
    hwmon.poll_sensors();
    uncore.poll_sensors();
}

float RAPL::get_power_usage() {
    return get_power(hwmon.get_sensor_value("energy"), previous_usage);
}

float RAPL::get_uncore_power_usage() {
    if (!uncore.is_open("energy"))
        return 0.f;

    return get_power(uncore.get_sensor_value("energy"), previous_uncore_usage);
}

float RAPL::get_power(uint64_t total_usage, uint64_t& previous) {
    if (previous == 0 || total_usage == previous) {
        previous = total_usage;
        return 0.f;
    }

    float usage = total_usage - previous;
    usage /= std::chrono::duration_cast<std::chrono::milliseconds>(delta_time_ns).count();
    usage /= 1'000.f;

    previous = total_usage;

    if (usage == std::numeric_limits<float>::infinity())
        return 0.f;

    return usage;
}
