#include <spdlog/spdlog.h>
#include <numeric>
#include <sstream>
#include <cmath>

#include "cpu.hpp"
#include "power/rapl.hpp"
#include "power/zenpower.hpp"
#include "power/zenergy.hpp"

CPU::CPU() {
    ifs_stat.open("/proc/stat");
    ifs_cpuinfo.open("/proc/cpuinfo");

    if (!ifs_stat.is_open())
        SPDLOG_WARN("failed to open cpu stats file. cpu load will not work.");

    if (!ifs_cpuinfo.is_open())
        SPDLOG_WARN("failed to open cpu info file. cpu frequency will not work.");

    power_usage = init_power_usage();
    temperature.find_temperature_sensor();
}

std::unique_ptr<CPUPower> CPU::init_power_usage() {
    std::unique_ptr<CPUPower> tmp_usage = std::make_unique<Zenpower>();

    if (tmp_usage->is_initialized()) {
        SPDLOG_INFO("Using zenpower for cpu power");
        return tmp_usage;
    }

    tmp_usage = std::make_unique<Zenergy>();

    if (tmp_usage->is_initialized()) {
        SPDLOG_INFO("Using zenergy for cpu power");
        return tmp_usage;
    }

    tmp_usage = std::make_unique<RAPL>();

    if (tmp_usage->is_initialized()) {
        SPDLOG_INFO("Using RAPL for cpu power");
        return tmp_usage;
    }

    return nullptr;
}

void CPU::poll() {
    pre_poll_overrides();
    poll_load();
    poll_frequency();
    poll_power_usage();
    poll_temperature();
}

cpu_info_t CPU::get_info() {
    return info;
}

std::vector<core_info_t> CPU::get_core_info() {
    return cores;
}

std::vector<std::vector<uint64_t>> CPU::get_cpu_times() {
    if (!ifs_stat.is_open())
        return {};

    ifs_stat.clear();
    ifs_stat.seekg(0);

    std::vector<std::vector<uint64_t>> times;

    for (std::string line; std::getline(ifs_stat, line);) {
        if (line.substr(0, 3) != "cpu")
            continue;

        std::stringstream ss(line);
        ss.seekg(5);

        std::vector<uint64_t> cpu_times;

        size_t time = 0;
        while (ss >> time) {
            cpu_times.push_back(time);
            time = 0;
        }

        times.push_back(cpu_times);
    }

    return times;
}

bool CPU::get_cpu_times(
    const std::vector<uint64_t>& cpu_times, uint64_t &idle_time, uint64_t &total_time
) {
    if (cpu_times.size() < 4)
        return false;

    idle_time = cpu_times[3];
    total_time = std::accumulate(cpu_times.begin(), cpu_times.end(), 0);
    return true;
}

void CPU::poll_load() {
    std::vector<std::vector<uint64_t>> cpu_times = get_cpu_times();

    for (size_t i = 0; i < cpu_times.size(); i++) {
        uint64_t idle_time = 0;
        uint64_t total_time = 0;

        if (!get_cpu_times(cpu_times[i], idle_time, total_time))
            continue;

        if (i > 0 && cores.size() <= i - 1)
            cores.push_back({});

        if (prev_idle_times.size() <= i)
            prev_idle_times.push_back({});

        if (prev_total_times.size() <= i)
            prev_total_times.push_back({});

        float idle_time_delta  = idle_time  - prev_idle_times[i];
        float total_time_delta = total_time - prev_total_times[i];
        float utilization      = 100.0 * (1.f - idle_time_delta / total_time_delta);

        prev_idle_times[i]  = idle_time;
        prev_total_times[i] = total_time;

        if (i == 0)
            info.load = std::round(utilization);
        else {
            cores[i - 1].load = std::round(utilization);
        }
    }
}

void CPU::poll_frequency() {
    if (!ifs_cpuinfo.is_open())
        return;

    ifs_cpuinfo.clear();
    ifs_cpuinfo.seekg(0);

    size_t cur_core = 0;

    for (std::string line; std::getline(ifs_cpuinfo, line);) {
        if (line.empty() || line.find(":") + 1 == line.length())
            continue;

        std::string key = line.substr(0, line.find(":") - 2);
        std::string val = line.substr(key.length() + 3);

        if (key != "cpu MHz")
            continue;

        if (cores.size() < cur_core + 1)
            cores.push_back({});

        cores[cur_core].frequency = std::round(std::stof(val));
        cur_core++;
    }

    // cpu frequency is equal to maximum frequency of one of its cores
    int max_frequency = 0;
    for (core_info_t& core : cores)
        if (core.frequency > max_frequency)
            max_frequency = core.frequency;

    info.frequency = max_frequency;
}

void CPU::poll_power_usage() {
    if (!power_usage)
        return;

    power_usage->poll();
    info.power = power_usage->get_power_usage();
}

void CPU::poll_temperature() {
    info.temp = temperature.get_temperature();
}

void CPUPower::poll() {
    std::chrono::time_point current_time = std::chrono::steady_clock::now();

    delta_time_ns = current_time - previous_time;
    previous_time = current_time;

    pre_poll_overrides();
}

void CPUTemp::find_temperature_sensor() {
    for (const cpu_temp_sensor& p : sensors) {
        const std::string& name = p.name;
        const hwmon_sensor& s = p.sensor;

        std::string hwmon_dir = hwmon.find_hwmon_dir_by_name(name);

        if (hwmon_dir.empty())
            continue;

        std::vector<hwmon_sensor> try_sensors = { {
            .generic_name = "temperature",
            .filename     = s.filename,
            .label        = s.label
        } };

        hwmon.base_dir = hwmon_dir;
        hwmon.setup(try_sensors);

        if (hwmon.is_open("temperature")) {
            SPDLOG_INFO(
                "Using {} ({}) for cpu temperature", name, hwmon.get_sensor_path("temperature")
            );

            found_sensor = true;
            return;
        }
    }
}

int CPUTemp::get_temperature() {
    if (!found_sensor)
        return 0;

    hwmon.poll_sensors();
    float temp = hwmon.get_sensor_value("temperature") / 1'000.f;
    return std::round(temp);
}
