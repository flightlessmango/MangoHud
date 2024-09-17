#pragma once

class NVIDIA {
    public:
        std::shared_ptr<Throttling> throttling;
        
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
        std::vector<int> pids() {
            std::vector<int> vec;
            for (size_t i = 0; i < infoCount; i++)
                vec.push_back(static_cast<int> (process_info[i].pid));

            return vec;
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
#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
        std::unique_ptr<Display, std::function<void(Display*)>> display;
        int num_coolers;
        int64_t get_nvctrl_fan_speed();
#endif
#ifdef HAVE_NVML
        unsigned int infoCount = 0;
        nvmlProcessInfo_t *process_info = new nvmlProcessInfo_t[infoCount];
        nvmlDevice_t device;
        void get_instant_metrics_nvml(struct gpu_metrics *metrics);
#endif
        bool nvml_available;
        bool nvctrl_available;
        bool failed;
        
        std::mutex metrics_mutex;
        gpu_metrics metrics;
        std::thread thread;
        std::condition_variable cond_var;
        std::atomic<bool> stop_thread = false;
        std::atomic<bool> paused = false;
#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
        void get_instant_metrics_xnvctrl(struct gpu_metrics *metrics);
        void parse_token(std::string token, std::unordered_map<std::string, std::string>& options);
        bool find_nv_x11(libnvctrl_loader& nvctrl, Display*& dpy);
        char* get_attr_target_string(libnvctrl_loader& nvctrl, int attr, int target_type, int target_id);
#endif
};