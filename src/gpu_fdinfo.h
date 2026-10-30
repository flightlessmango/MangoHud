#pragma once

#include <spdlog/spdlog.h>
#include "gpu_metrics_util.h"
#include "../mangohud-next/legacy_gpu_wrapper/wrapper.hpp"

class GPU_fdinfo {
private:
    pid_t pid = getpid();

    const std::string driver;
    const std::string pci_dev;
    const std::string drm_node;

    std::thread thread;
    std::condition_variable cond_var;

    std::atomic<bool> stop_thread { false };
    std::atomic<bool> paused { false };

    struct gpu_metrics metrics;
    mutable std::mutex metrics_mutex;

    LegacyGPUWrapper gpu;

    void main_thread();

public:
    GPU_fdinfo(
        const std::string driver, const std::string pci_dev, const std::string drm_node,
        uint32_t device_id, uint32_t vendor_id
    );

    ~GPU_fdinfo() {
        {
            // under the lock and with a notify: a paused sampler sleeps on
            // cond_var and would never see stop_thread
            std::lock_guard<std::mutex> lock(metrics_mutex);
            stop_thread = true;
        }
        cond_var.notify_all();
        if (thread.joinable())
            thread.join();
    }

    gpu_metrics copy_metrics() const
    {
        return metrics;
    };

    void pause()
    {
        paused = true;
        cond_var.notify_one();
    }

    void resume()
    {
        paused = false;
        cond_var.notify_one();
    }
};
