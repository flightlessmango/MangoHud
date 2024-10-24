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

void GPU_fdinfo::get_load() {
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        static uint64_t previous_gpu_time, previous_time, now, gpu_time_now;
        gpu_time_now = get_gpu_time();
        now = os_time_get_nano();

        if (gpu_time_now > previous_gpu_time &&
            now - previous_time > METRICS_UPDATE_PERIOD_MS * 1'000'000){
            float time_since_last = now - previous_time;
            float gpu_since_last = gpu_time_now - previous_gpu_time;
            auto result = int((gpu_since_last / time_since_last) * 100);
            if (result > 100)
                result = 100;

            metrics.load = result;
            previous_gpu_time = gpu_time_now;
            previous_time = now;
        }
    }
}
