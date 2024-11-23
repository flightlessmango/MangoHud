#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

std::string GPU_fdinfo::get_drm_engine_type() {
    std::string drm_type = "drm-engine-";

    if (strstr(module, "amdgpu"))
        drm_type += "gfx";
    else if (strstr(module, "i915"))
        drm_type += "render";
    else if (strstr(module, "msm"))
        drm_type += "gpu";
    else
        drm_type += "none";

    return drm_type;
}

std::string GPU_fdinfo::get_drm_memory_type() {
    std::string drm_type = "drm-";

    // msm driver does not report vram usage

    if (strstr(module, "amdgpu"))
        drm_type += "memory-vram";
    else if (strstr(module, "i915"))
        drm_type += "total-local0";
    else
        drm_type += "memory-none";

    return drm_type;
}

void GPU_fdinfo::find_fd() {
#ifdef __linux__
    DIR* dir = opendir("/proc/self/fdinfo");
    if (!dir) {
        perror("Failed to open directory");
    }

    for (const auto& entry : fs::directory_iterator("/proc/self/fdinfo")){
        FILE* file = fopen(entry.path().string().c_str(), "r");

        if (!file) continue;

        char line[256];
        bool found_driver = false;
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, module) != NULL)
                found_driver = true;

            if (found_driver) {
                if(strstr(line, get_drm_engine_type().c_str())) {
                    fdinfo.push_back(file);
                    break;
                }
            }
        }

        if (!found_driver)
            fclose(file);
    }

    closedir(dir);
#endif
}

uint64_t GPU_fdinfo::get_gpu_time() {
    char line[256];
    uint64_t total_val = 0;
    for (auto fd : fdinfo) {
        rewind(fd);
        fflush(fd);
        uint64_t val = 0;
        while (fgets(line, sizeof(line), fd)){
            std::string scan_str = get_drm_engine_type() + ": %" SCNu64 " ns";

            if (sscanf(line, scan_str.c_str(), &val) == 1) {
                total_val += val;
                break;
            }
        }
    }

    return total_val;
}

float GPU_fdinfo::get_vram_usage() {
    char line[256];
    uint64_t total_val = 0;

    for (auto fd : fdinfo) {
        rewind(fd);
        fflush(fd);

        uint64_t val = 0;

        while (fgets(line, sizeof(line), fd)) {
            std::string scan_str = get_drm_memory_type() + ": %llu KiB";

            if (sscanf(line, scan_str.c_str(), &val) == 1) {
                total_val += val;
                break;
            }
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

    return energy_input / 1'000'000;
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
            float power_usage_since_last = power_usage_now - previous_power_usage;

            auto result = int((gpu_since_last / time_since_last) * 100);
            if (result > 100)
                result = 100;

            metrics.load = result;
            metrics.memoryUsed = get_vram_usage();
            metrics.powerUsage = power_usage_since_last;

            previous_gpu_time = gpu_time_now;
            previous_time = now;
            previous_power_usage = power_usage_now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(METRICS_UPDATE_PERIOD_MS));
    }
}
