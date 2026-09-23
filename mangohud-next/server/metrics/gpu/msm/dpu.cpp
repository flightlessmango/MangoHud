#include "dpu.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include "../../../../../src/string_utils.h"

namespace fs = std::filesystem;

MSM_DPU::MSM_DPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-msm-dpu"), FDInfo(drm_node) {
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("gpu");
    hwmon.setup(sensors, drm_node);
    junction_temp_file = open_thermal_zone("gpuss-0-thermal");
    memory_temp_file = open_thermal_zone("ddr-thermal");
    core_clock_file = open_core_clock();
}

void MSM_DPU::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
}

int MSM_DPU::get_temperature() {
    return static_cast<int>(::lroundf(hwmon.get_sensor_value("temp") / 1000.0f));
}

std::ifstream MSM_DPU::open_thermal_zone(const std::string& type) {
    std::ifstream file;
    const fs::path sysfs_thermal = "/sys/class/thermal";

    if (!fs::exists(sysfs_thermal))
        return file;

    for (auto& entry : fs::directory_iterator(sysfs_thermal)) {
        if (!entry.path().filename().string().starts_with("thermal_zone"))
            continue;

        std::ifstream type_file(entry.path() / "type");
        std::string zone_type;
        std::getline(type_file, zone_type);
        if (zone_type != type)
            continue;

        file.open(entry.path() / "temp");
        if (file.is_open())
            SPDLOG_INFO("thermal: using {} input: {}", type, (entry.path() / "temp").string());
        return file;
    }

    return file;
}

std::ifstream MSM_DPU::open_core_clock() {
    std::ifstream file;
    const fs::path sysfs_devfreq = "/sys/class/devfreq";

    if (!fs::exists(sysfs_devfreq))
        return file;

    for (auto& entry : fs::directory_iterator(sysfs_devfreq)) {
        std::string name = entry.path().filename().string();
        if (!name.ends_with(".gpu"))
            continue;

        fs::path cur_freq = entry.path() / "cur_freq";
        file.open(cur_freq);
        if (file.is_open())
            SPDLOG_INFO("devfreq: using gpu clock input: {}", cur_freq.string());
        return file;
    }

    return file;
}

int MSM_DPU::read_thermal_zone(std::ifstream& file) {
    if (!file.is_open())
        return 0;

    file.clear();
    file.seekg(0, std::ios::beg);

    int temp = 0;
    if (!(file >> temp))
        return 0;

    return static_cast<int>(::lroundf(temp / 1000.0f));
}

int MSM_DPU::get_junction_temperature() {
    return read_thermal_zone(junction_temp_file);
}

int MSM_DPU::get_memory_temp() {
    return read_thermal_zone(memory_temp_file);
}

int MSM_DPU::get_core_clock() {
    if (!core_clock_file.is_open())
        return 0;

    core_clock_file.clear();
    core_clock_file.seekg(0, std::ios::beg);

    int clock = 0;
    if (!(core_clock_file >> clock))
        return 0;

    return clock / 1'000'000;
}

float MSM_DPU::get_power_usage() {
    std::ifstream file("/run/power-monitor/power/gfx");
    if (file.fail())
        return 0.0f;

    std::string value;
    std::getline(file, value);
    if (value.empty())
        return 0.0f;

    float power = 0.0f;
    if (try_stof(power, value))
        return power;

    return 0.0f;
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

float MSM_DPU::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-resident-memory");
}
