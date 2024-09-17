#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

void GPU_fdinfo::find_fd() {
#if DETECT_OS_UNIX
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
                if(strstr(line, "drm-engine-gpu")) {
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
            if (sscanf(line, "drm-engine-gpu: %" SCNu64 " ns", &val) == 1) {
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

        if (previous_time && previous_gpu_time && gpu_time_now > previous_gpu_time){
            float time_since_last = now - previous_time;
            float gpu_since_last = gpu_time_now - previous_gpu_time;
            auto result = int((gpu_since_last / time_since_last) * 100);
            if (result > 100)
                result = 100;

            metrics.load = result;
            previous_gpu_time = gpu_time_now;
            previous_time = now;
        } else {
            previous_gpu_time = gpu_time_now;
            previous_time = now;
        }
    }
}
