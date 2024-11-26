#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

void GPU_fdinfo::find_fd()
{
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

uint64_t GPU_fdinfo::get_gpu_time()
{
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

float GPU_fdinfo::get_memory_used()
{
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

void GPU_fdinfo::find_intel_hwmon()
{
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

float GPU_fdinfo::get_current_power()
{
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

float GPU_fdinfo::get_power_usage()
{
    static float last;
    float now = get_current_power();

    float delta = now - last;
    delta /= (float)METRICS_UPDATE_PERIOD_MS / 1000;

    last = now;

    return delta;
}

int GPU_fdinfo::get_gpu_load()
{
    static uint64_t previous_gpu_time, previous_time;

    uint64_t now = os_time_get_nano();
    uint64_t gpu_time_now = get_gpu_time();

    float delta_time = now - previous_time;
    float delta_gpu_time = gpu_time_now - previous_gpu_time;

    int result = std::lround(delta_gpu_time / delta_time * 100);

    if (result > 100)
        result = 100;

    previous_gpu_time = gpu_time_now;
    previous_time = now;

    return result;
}

void GPU_fdinfo::main_thread()
{
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        metrics.load = get_gpu_load();
        metrics.memoryUsed = get_memory_used();
        metrics.powerUsage = get_power_usage();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(METRICS_UPDATE_PERIOD_MS));
    }
}
