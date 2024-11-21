#pragma once
#include <sys/stat.h>
#include <thread>
#include <filesystem.h>
#include <inttypes.h>
#ifdef TEST_ONLY
#include <../src/mesa/util/os_time.h>
#else
#include <mesa/util/os_time.h>
#endif
#include <spdlog/spdlog.h>
#include "gpu_metrics_util.h"
#include <atomic>

class GPU_fdinfo {
    private:
        bool init = false;
        struct gpu_metrics metrics;
        std::vector<std::ifstream> fdinfo;
        const std::string module;
        const std::string pci_dev;
        void find_fd();
        void find_intel_hwmon();
        std::ifstream energy_stream;
        std::thread thread;
        std::condition_variable cond_var;
        std::atomic<bool> stop_thread{false};
        std::atomic<bool> paused{false};
        mutable std::mutex metrics_mutex;

        uint64_t get_gpu_time();
        void get_load();
        std::string drm_engine_type = "EMPTY";
        std::string drm_memory_type = "EMPTY";
        float get_vram_usage();
        float get_power_usage();

    public:
        GPU_fdinfo(const std::string module, const std::string pci_dev) : module(module), pci_dev(pci_dev) {
            if (module == "i915") {
                drm_engine_type = "drm-engine-render";
                drm_memory_type = "drm-total-local0";
                find_intel_hwmon();
            } else if (module == "amdgpu") {
                drm_engine_type = "drm-engine-gfx";
                drm_memory_type = "drm-memory-vram";
            } else if (module == "msm") {
                // msm driver does not report vram usage
                drm_engine_type = "drm-engine-gpu";
            }

            find_fd();

            std::thread thread(&GPU_fdinfo::get_load, this);
            thread.detach();
        }

        gpu_metrics copy_metrics() const {
            return metrics;
        };

        void pause() {
            paused = true;
            cond_var.notify_one();
        }

        void resume() {
            paused = false;
            cond_var.notify_one();
        }
};
