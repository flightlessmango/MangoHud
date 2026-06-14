#include <filesystem>
#include "xe.hpp"
#include <cmath>

Intel_xe::Intel_xe(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-intel-xe"), FDInfo(drm_node) {
    hwmon.setup(sensors, drm_node);
    drm_available = drm.setup("/dev/dri/by-path/pci-" + pci_dev + "-card");
    find_gt_dir();
}

void Intel_xe::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
    drm.poll();
    throttling = get_throttling_status();
}

float Intel_xe::get_vram_used() {
    if (!drm_available)
        return 0.f;

    return drm.get_used_memory() / 1024.f / 1024.f / 1024.f;
}

float Intel_xe::get_memory_total() {
    if (!drm_available)
        return 0.f;

    return drm.get_total_memory() / 1024.f / 1024.f / 1024.f;
}

int Intel_xe::get_memory_temp() {
    return std::round(hwmon.get_sensor_value("vram_temp") / 1000.f);
}

int Intel_xe::get_temperature() {
    return std::round(hwmon.get_sensor_value("temp") / 1000.f);
}

int Intel_xe::get_core_clock() {
    if (!ifs_gpu_clock.is_open())
        return 0;

    std::string clock_str;

    ifs_gpu_clock.seekg(0);

    std::getline(ifs_gpu_clock, clock_str);

    if (clock_str.empty())
        return 0;

    return std::stoi(clock_str);
}

int Intel_xe::get_voltage() {
    return hwmon.get_sensor_value("voltage");
}

float Intel_xe::get_power_usage() {
    uint64_t current_power_usage = hwmon.get_sensor_value("energy");

    if (!previous_power_usage) {
        previous_power_usage = current_power_usage;
        return 0;
    }

    float delta = current_power_usage - previous_power_usage;
    delta /= std::chrono::duration_cast<std::chrono::milliseconds>(delta_time_ns).count();
    delta *= 1000.f;

    previous_power_usage = current_power_usage;

    return delta / 1'000'000.f;
}

float Intel_xe::get_power_limit() {
    float limit = hwmon.get_sensor_value("power_limit") / 1'000'000.f;
    return limit;
}

bool Intel_xe::get_is_power_throttled() {
    return throttling & GPU_throttle_status::POWER;
}

bool Intel_xe::get_is_current_throttled() {
    return throttling & GPU_throttle_status::CURRENT;
}

bool Intel_xe::get_is_temp_throttled() {
    return throttling & GPU_throttle_status::TEMP;
}

bool Intel_xe::get_is_other_throttled() {
    return throttling & GPU_throttle_status::OTHER;
}

int Intel_xe::get_fan_speed() {
    return hwmon.get_sensor_value("fan_speed");
}

int Intel_xe::get_process_load(pid_t pid) {
    if (fdinfo.pids.find(pid) == fdinfo.pids.end())
        return 0.f;

    FDInfoBase& p = fdinfo.pids.at(pid);

    double load = 0;

    for (fdinfo_data& fd : p.fds_data) {
        std::string client_id = fd["drm-client-id"];
        std::string cur_cycles_str = fd["drm-cycles-rcs"];
        std::string cur_total_cycles_str = fd["drm-total-cycles-rcs"];

        if (
            client_id.empty() || cur_cycles_str.empty() ||
            cur_total_cycles_str.empty()
        )
            continue;

        uint64_t cur_cycles = std::stoull(cur_cycles_str);
        uint64_t cur_total_cycles = std::stoull(cur_total_cycles_str);

        if (previous_cycles.find(client_id) == previous_cycles.end()) {
            previous_cycles[client_id] = { cur_cycles, cur_total_cycles };
            continue;
        }

        uint64_t prev_cur_cycles = previous_cycles[client_id].first;
        uint64_t prev_total_cycles = previous_cycles[client_id].second;

        uint64_t delta_cycles = cur_cycles - prev_cur_cycles;
        uint64_t delta_total_cycles = cur_total_cycles - prev_total_cycles;

        previous_cycles[client_id] = { cur_cycles, cur_total_cycles };

        if (delta_cycles <= 0 || delta_total_cycles <= 0)
            continue;

        double fd_load = static_cast<double>(delta_cycles) / delta_total_cycles * 100.f;
        load += fd_load;
    }

    if (load > 100.f)
        load = 100.f;

    return std::lround(load);
}

float Intel_xe::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-resident-vram0");
}

float Intel_xe::get_process_gtt_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-resident-gtt");
}

void Intel_xe::find_gt_dir()
{
    const std::string device = "/sys/class/drm/" + drm_node + "/device/tile0";
    std::string gt_dir;

    if (!fs::exists(device)) {
        SPDLOG_WARN(
            "\"{}\" doesn't exist. GPU clock and throttling status will be unavailable.",
            device
        );
        return;
    }

    // Check every "gt" dir if it has "engines/rcs" inside
    for (const auto& entry : fs::directory_iterator(device)) {
        auto path = entry.path().string();

        if (path.substr(device.size() + 1, 2) != "gt")
            continue;

        SPDLOG_DEBUG("Checking \"{}\" for rcs.", path);

        if (!fs::exists(path + "/engines/rcs")) {
            SPDLOG_DEBUG("Skipping \"{}\" because rcs doesn't exist.", path);
            continue;
        }

        SPDLOG_DEBUG("Found rcs in \"{}\"", path);
        gt_dir = path;
        break;
    }

    if (gt_dir.empty()) {
        SPDLOG_WARN(
            "rcs not found inside \"{}\". GPU clock will not be available.",
            device
        );
        return;
    }

    std::string gpu_clock_path = gt_dir + "/freq0/act_freq";
    ifs_gpu_clock.open(gpu_clock_path);

    if (!ifs_gpu_clock.good())
        SPDLOG_WARN("Intel xe gt dir: failed to open {}", gpu_clock_path);

    std::string throttle_folder = gt_dir + "/freq0/throttle/";
    std::string throttle_status_path = throttle_folder + "status";

    ifs_throttle_status.open(throttle_status_path);

    if (!ifs_throttle_status.good()) {
       SPDLOG_WARN("Intel xe gt dir: failed to open {}", throttle_status_path);
       return;
    }

    load_throttle_reasons(throttle_folder, throttle_power  , ifs_throttle_power);
    load_throttle_reasons(throttle_folder, throttle_current, ifs_throttle_current);
    load_throttle_reasons(throttle_folder, throttle_temp   , ifs_throttle_temp);
}

void Intel_xe::load_throttle_reasons(
    std::string throttle_folder, std::vector<std::string> throttle_reasons,
    std::vector<std::ifstream>& ifs_throttle_reason
) {
    for (const auto& throttle_reason : throttle_reasons) {
        std::string throttle_path = throttle_folder + throttle_reason;

        if (!fs::exists(throttle_path)) {
            SPDLOG_WARN(
                "Throttle file {} not found",
                throttle_path
            );
            continue;
        }

        std::ifstream throttle_stream(throttle_path);

        if (!throttle_stream.good()) {
            SPDLOG_WARN("failed to open {}", throttle_path);
            continue;
        }

        ifs_throttle_reason.push_back(std::move(throttle_stream));
    }
}

bool Intel_xe::check_throttle_reasons(std::vector<std::ifstream>& ifs_throttle_reason) {
    for (auto& throttle_reason_stream : ifs_throttle_reason) {
        std::string throttle_reason_str;
        throttle_reason_stream.seekg(0);
        std::getline(throttle_reason_stream, throttle_reason_str);

        if (throttle_reason_str == "1")
            return true;
    }

    return false;
}

int Intel_xe::get_throttling_status() {
    if (!ifs_throttle_status.is_open())
        return 0;

    std::string throttle_status_str;
    ifs_throttle_status.seekg(0);
    std::getline(ifs_throttle_status, throttle_status_str);

    if (throttle_status_str != "1")
        return 0;

    int reasons =
        check_throttle_reasons(ifs_throttle_power) * GPU_throttle_status::POWER +
        check_throttle_reasons(ifs_throttle_current) * GPU_throttle_status::CURRENT +
        check_throttle_reasons(ifs_throttle_temp) * GPU_throttle_status::TEMP;

    // No throttle reasons for OTHER currently
    if (reasons == 0)
        reasons |= GPU_throttle_status::OTHER;

    return reasons;
}
