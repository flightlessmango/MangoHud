#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

void GPU_fdinfo::find_fd() {
    auto path = fs::path("/proc/self/fdinfo");

    if (!fs::exists(path)) {
        SPDLOG_DEBUG("{} does not exist", path.string());
        return;
    }

    std::vector<std::string> fds_to_open;
    for (const auto& entry : fs::directory_iterator(path)) {
        auto fd_path = entry.path().string();
        auto file = std::ifstream(fd_path);

        if (!file.is_open())
            continue;

        bool found_driver = false;
        for (std::string line; std::getline(file, line);) {
            if (line.find(module) != std::string::npos)
                found_driver = true;

            if (found_driver && line.find(drm_engine_type) != std::string::npos) {
                fds_to_open.push_back(fd_path);
                break;
            }
        }
    }

    for (const auto& fd : fds_to_open)
        fdinfo.push_back(std::ifstream(fd));
}

uint64_t GPU_fdinfo::get_gpu_time() {
    uint64_t total_val = 0;

    for (auto& fd : fdinfo) {
        fd.clear();
        fd.seekg(0);

        for (std::string line; std::getline(fd, line);) {
            if (line.find(drm_engine_type) == std::string::npos)
                continue;

            auto start = (drm_engine_type + ": ").length();
            total_val += std::stoull(line.substr(start));
        }
    }

    return total_val;
}

float GPU_fdinfo::get_vram_usage() {
    uint64_t total_val = 0;

    for (auto& fd : fdinfo) {
        fd.clear();
        fd.seekg(0);

        for (std::string line; std::getline(fd, line);) {
            if (line.find(drm_memory_type) == std::string::npos)
                continue;

            auto start = (drm_memory_type + ": ").length();
            total_val += std::stoull(line.substr(start));
        }
    }

    return (float)total_val / 1024 / 1024;
}

void GPU_fdinfo::find_intel_hwmon() {
    std::string device = "/sys/bus/pci/devices/";
    device += pci_dev;
    device += "/hwmon";

    if (!fs::exists(device)) {
        SPDLOG_DEBUG("Intel hwmon directory {} doesn't exist.", device);
        return;
    }

    auto dir_iterator = fs::directory_iterator(device);
    auto hwmon = dir_iterator->path().string();

    if (hwmon.empty()) {
        SPDLOG_DEBUG("Intel hwmon directory is empty.");
        return;
    }

    hwmon += "/energy1_input";

    if (!fs::exists(hwmon)) {
        SPDLOG_DEBUG("Intel hwmon: file {} doesn't exist.", hwmon);
        return;
    }

    SPDLOG_DEBUG("Intel hwmon found: hwmon = {}", hwmon);

    energy_stream.open(hwmon);

    if (!energy_stream.good())
        SPDLOG_DEBUG("Intel hwmon: failed to open {}", hwmon);
}

float GPU_fdinfo::get_power_usage() {
    if (!energy_stream.is_open())
        return 0.f;

    std::string energy_input_str;
    uint64_t energy_input;

    energy_stream.seekg(0);

    std::getline(energy_stream, energy_input_str);

    if (energy_input_str.empty())
        return 0.f;

    energy_input = std::stoull(energy_input_str);

    return (float)energy_input / 1'000'000;
}

void GPU_fdinfo::get_load() {
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        static uint64_t previous_gpu_time, previous_time, now, gpu_time_now;
        static float power_usage_now, previous_power_usage;

        now = os_time_get_nano();
        gpu_time_now = get_gpu_time();
        power_usage_now = get_power_usage();

        if (gpu_time_now > previous_gpu_time) {
            float time_since_last = now - previous_time;
            float gpu_since_last = gpu_time_now - previous_gpu_time;
            float power_usage_since_last =
                (power_usage_now - previous_power_usage) /
                ((float)METRICS_UPDATE_PERIOD_MS / 1000);

            auto result = int((gpu_since_last / time_since_last) * 100);
            if (result > 100)
                result = 100;

            metrics.load = result;
            metrics.memoryUsed = get_vram_usage();
            metrics.powerUsage = power_usage_since_last;

            previous_gpu_time = gpu_time_now;
            previous_power_usage = power_usage_now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(METRICS_UPDATE_PERIOD_MS)
        );
    }
}
