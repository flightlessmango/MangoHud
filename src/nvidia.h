#pragma once
#include "gpu.h"
#ifdef HAVE_NVML
#include "loaders/loader_nvml.h"
#endif

class NVIDIA {
    public:
        std::shared_ptr<Throttling> throttling;

        bool nvml_available = false;
        bool nvctrl_available = false;

        gpu_metrics copy_metrics() {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            return metrics;
        };

        void get_samples_and_copy();

        NVIDIA(const char* pciBusId);
        ~NVIDIA() {
            stop_thread = true;
            if (thread.joinable())
                thread.join();
        };

#ifdef HAVE_NVML
        void nvml_get_process_info() {
            if (!nvml_available || !nvml)
                return;

            unsigned int infoCount = 0;

            std::vector<nvmlProcessInfo_v1_t> cur_process_info(infoCount);
            nvmlReturn_t ret = nvml->nvmlDeviceGetGraphicsRunningProcesses(device, &infoCount, cur_process_info.data());

            if (ret != NVML_ERROR_INSUFFICIENT_SIZE)
                return;

            cur_process_info.resize(infoCount);
            ret = nvml->nvmlDeviceGetGraphicsRunningProcesses(device, &infoCount, cur_process_info.data());

            if (ret != NVML_SUCCESS)
                return;

            process_info = cur_process_info;
        };

        std::vector<int> pids() {
            std::vector<int> vec;

            for (const auto& proc : process_info)
                vec.push_back(static_cast<int> (proc.pid));

            return vec;
        };

        float get_proc_vram() {
            for (const auto& proc : process_info) {
                if (static_cast<pid_t>(proc.pid) != pid)
                    continue;

                return static_cast<float>(proc.usedGpuMemory);
            }

            return 0.f;
        };
#endif        

        void pause() {
            paused = true;
            cond_var.notify_one();
        };

        void resume() {
            paused = false;
            cond_var.notify_one();
        }

    private:
        pid_t pid = getpid();
        std::mutex metrics_mutex;
        gpu_metrics metrics;
        std::thread thread;
        std::condition_variable cond_var;
        std::atomic<bool> stop_thread{false};
        std::atomic<bool> paused{false};

#ifdef HAVE_NVML
        nvmlDevice_t device;

        std::vector<nvmlProcessInfo_v1_t> process_info = {};

        void get_instant_metrics_nvml(struct gpu_metrics *metrics);
        std::shared_ptr<libnvml_loader> nvml = get_libnvml_loader();
#endif
};
