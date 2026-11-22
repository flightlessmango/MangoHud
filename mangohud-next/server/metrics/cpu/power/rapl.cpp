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

    std::ifstream range_file(hwmon.base_dir + "/max_energy_range_uj");
    range_file >> package_counter.max_range;

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
        std::ifstream range_file(uncore.base_dir + "/max_energy_range_uj");
        range_file >> uncore_counter.max_range;

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
    return get_power(hwmon.get_sensor_value("energy"), package_counter);
}

float RAPL::get_uncore_power_usage() {
    if (!uncore.is_open("energy"))
        return 0.f;

    return get_power(uncore.get_sensor_value("energy"), uncore_counter);
}

float RAPL::get_power(uint64_t total_usage, Counter& counter) {
    const auto previous = counter.previous;
    counter.previous = total_usage;
    if (!counter.primed) {
        counter.primed = true;
        return 0.f;
    }

    uint64_t delta = 0;
    if (total_usage >= previous)
        delta = total_usage - previous;
    else if (counter.max_range > previous && total_usage < counter.max_range)
        delta = counter.max_range - previous + total_usage;
    else
        return 0.f;

    const double elapsed_us = std::chrono::duration<double, std::micro>(delta_time_ns).count();
    if (elapsed_us <= 0)
        return 0.f;

    return static_cast<float>(delta / elapsed_us);
}
