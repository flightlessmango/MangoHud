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
        std::vector<FILE*> fdinfo;
        const char* module;
        void find_fd();
        std::thread thread;
        std::condition_variable cond_var;
        std::atomic<bool> stop_thread{false};
        std::atomic<bool> paused{false};
        mutable std::mutex metrics_mutex;

        uint64_t get_gpu_time();
        void get_load();

    public:
        GPU_fdinfo(const char* module) : module(module) {
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

        ~GPU_fdinfo() {
            for (size_t i = 0; i < fdinfo.size(); i++) {
                fclose(fdinfo[i]);
            }
            fdinfo.clear();
        }
};
