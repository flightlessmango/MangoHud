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

    for (const auto& fd : fds_to_open) {
        fdinfo.push_back(std::ifstream(fd));

        if (module == "xe")
            xe_fdinfo_last_cycles.push_back(0);
    }
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

    hwmon += module == "i915" ? "/energy1_input" : "/energy2_input";

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

std::pair<uint64_t, uint64_t> GPU_fdinfo::get_gpu_time_xe()
{
    uint64_t total_cycles = 0, total_total_cycles = 0;

    for (size_t i = 0; i < fdinfo.size(); i++) {
        fdinfo[i].clear();
        fdinfo[i].seekg(0);

        uint64_t current_cycles = 0, current_total_cycles = 0;

        for (std::string line; std::getline(fdinfo[i], line);) {
            if (line.find("drm-cycles-rcs") == std::string::npos &&
                line.find("drm-total-cycles-rcs") == std::string::npos
            )
                continue;

            auto drm_type = line.substr(0, line.find(":"));

            auto start = (drm_type + ": ").length();
            auto val = std::stoull(line.substr(start));

            if (drm_type == "drm-cycles-rcs")
                current_cycles = val;
            else if (drm_type == "drm-total-cycles-rcs")
                current_total_cycles = val;

            if (current_cycles > 0 && current_total_cycles > 0)
                break;
        }

        if (current_cycles > 0 && current_cycles != xe_fdinfo_last_cycles[i] &&
            current_total_cycles > 0)
        {
            total_cycles += current_cycles;
            total_total_cycles += current_total_cycles;

            xe_fdinfo_last_cycles[i] = current_cycles;
        }
    }

    return { total_cycles, total_total_cycles };
}

int GPU_fdinfo::get_xe_load()
{
    static uint64_t previous_cycles, previous_total_cycles;

    auto gpu_time = get_gpu_time_xe();
    uint64_t cycles = gpu_time.first;
    uint64_t total_cycles = gpu_time.second;

    uint64_t delta_cycles = cycles - previous_cycles;
    uint64_t delta_total_cycles = total_cycles - previous_total_cycles;

    if (delta_cycles == 0 || delta_total_cycles == 0)
        return 0;

    double load = (double)delta_cycles / delta_total_cycles * 100;

    previous_cycles = cycles;
    previous_total_cycles = total_cycles;

    // SPDLOG_DEBUG("cycles             = {}", cycles);
    // SPDLOG_DEBUG("total_cycles       = {}", total_cycles);
    // SPDLOG_DEBUG("delta_cycles       = {}", delta_cycles);
    // SPDLOG_DEBUG("delta_total_cycles = {}", delta_total_cycles);
    // SPDLOG_DEBUG("{} / {} * 100 = {}", delta_cycles, delta_total_cycles, load);
    // SPDLOG_DEBUG("load = {}\n", std::lround(load));

    return std::lround(load);
}

int GPU_fdinfo::get_gpu_load()
{
    static uint64_t previous_gpu_time, previous_time;

    if (module == "xe") {
        int result = get_xe_load();

        if (result > 100)
            result = 100;

        return result;
    }

    uint64_t now = os_time_get_nano();
    uint64_t gpu_time_now = get_gpu_time();

    float delta_time = now - previous_time;
    float delta_gpu_time = gpu_time_now - previous_gpu_time;

    int result = delta_gpu_time / delta_time * 100;

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
