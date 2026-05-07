#include <filesystem>
#include "i915.hpp"
#include <cmath>

Intel_i915::Intel_i915(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-intel-i915"), FDInfo(drm_node) {
    hwmon.setup(sensors, drm_node);
    drm_available = drm.setup("/dev/dri/by-path/pci-" + pci_dev + "-card");
    find_gt_dir();
}

void Intel_i915::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
    drm.poll();
    throttling = get_throttling_status();
}

float Intel_i915::get_vram_used() {
    if (!drm_available)
        return 0.f;

    return drm.get_used_memory() / 1024.f / 1024.f / 1024.f;
}

float Intel_i915::get_memory_total() {
    if (!drm_available)
        return 0.f;

    return drm.get_total_memory() / 1024.f / 1024.f / 1024.f;
}

int Intel_i915::get_temperature() {
    return std::round(hwmon.get_sensor_value("temp") / 1000.f);
}

float Intel_i915::get_power_usage() {
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

float Intel_i915::get_power_limit() {
    float limit = hwmon.get_sensor_value("power_limit") / 1'000'000.f;
    return limit;
}

int Intel_i915::get_core_clock() {
    if (!ifs_gpu_clock.is_open())
        return 0;

    std::string clock_str;

    ifs_gpu_clock.seekg(0);

    std::getline(ifs_gpu_clock, clock_str);

    if (clock_str.empty())
        return 0;

    return std::stoi(clock_str);
}

int Intel_i915::get_voltage() {
    return hwmon.get_sensor_value("voltage");
}

bool Intel_i915::get_is_power_throttled() {
    return throttling & GPU_throttle_status::POWER;
}

bool Intel_i915::get_is_current_throttled() {
    return throttling & GPU_throttle_status::CURRENT;
}

bool Intel_i915::get_is_temp_throttled() {
    return throttling & GPU_throttle_status::TEMP;
}

bool Intel_i915::get_is_other_throttled() {
    return throttling & GPU_throttle_status::OTHER;
}

int Intel_i915::get_fan_speed() {
    return hwmon.get_sensor_value("fan_speed");
}

int Intel_i915::get_process_load(pid_t pid) {
    uint64_t* previous_gpu_time = &previous_gpu_times[pid];
    uint64_t gpu_time_now = fdinfo.get_gpu_time(pid, "drm-engine-render");

    if (!*previous_gpu_time) {
        *previous_gpu_time = gpu_time_now;
        return 0;
    }

    float delta_gpu_time = gpu_time_now - *previous_gpu_time;
    float result = delta_gpu_time / delta_time_ns.count() * 100;

    if (result > 100.f)
        result = 100.f;

    *previous_gpu_time = gpu_time_now;

    return std::round(result);
}

float Intel_i915::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-total-local0");
}

float Intel_i915::get_process_gtt_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-total-system0");
}

void Intel_i915::find_gt_dir() {
    const std::string device = "/sys/class/drm/" + drm_node + "/device/drm";
    std::string gt_dir;

    // Find first dir which starts with name "card"
    for (const auto& entry : fs::directory_iterator(device)) {
        std::filesystem::path path = entry.path();

        if (path.filename().string().substr(0, 4) == "card") {
            gt_dir = path.string();
            break;
        }
    }

    if (gt_dir.empty()) {
        SPDLOG_WARN("failed to find gt dir. gpu clock and throttling status will not work");
        return;
    }

    ifs_gpu_clock.open(gt_dir + "/gt_act_freq_mhz");

    if (!ifs_gpu_clock.is_open())
        SPDLOG_WARN(
            "failed to open \"{}\". GPU clock will not be available!",
            gt_dir + "/gt_act_freq_mhz"
        );

    // Assuming gt0 since all recent GPUs have the RCS engine on gt0,
    // and latest GPUs need Xe anyway
    std::string throttle_folder = gt_dir + "/gt/gt0/throttle_";
    std::string throttle_status_path = throttle_folder + "reason_status";

    ifs_throttle_status.open(throttle_status_path);

    if (!ifs_throttle_status.is_open()) {
       SPDLOG_WARN("failed to open \"{}\". throttle status will not work", throttle_status_path);
       return;
    }

    load_throttle_reasons(throttle_folder, throttle_power  , ifs_throttle_power);
    load_throttle_reasons(throttle_folder, throttle_current, ifs_throttle_current);
    load_throttle_reasons(throttle_folder, throttle_temp   , ifs_throttle_temp);
}

void Intel_i915::load_throttle_reasons(
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

bool Intel_i915::check_throttle_reasons(std::vector<std::ifstream>& ifs_throttle_reason) {
    for (auto& throttle_reason_stream : ifs_throttle_reason) {
        std::string throttle_reason_str;
        throttle_reason_stream.seekg(0);
        std::getline(throttle_reason_stream, throttle_reason_str);

        if (throttle_reason_str == "1")
            return true;
    }

    return false;
}

int Intel_i915::get_throttling_status() {
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
