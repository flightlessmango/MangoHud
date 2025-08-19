#pragma once
#include <string>
#include "real_dlsym.h"
#include "spdlog/spdlog.h"
#include <vector>
#include "overlay_params.h"

class lsfg_vk {
    public:
        struct Configuration {
            bool enable{false};
            std::string dll;
            size_t multiplier{2};
            float flowScale{1.0F};
            bool performance{false};
            bool hdr{false};
        };

        bool inited = false;
        Configuration* conf;
        bool mangohud_is_receiving_all_lsfg_frames = false;
        float frametimes[200] = {};
        float combined_frametimes[200] = {};
        uint64_t combined_index = 0;
        std::mutex combined_lock;
        static void* lsfg_handle;

        lsfg_vk();
        void notify_extra_frame(uint64_t n_frames);
        bool is_active();
        void add_frame_to_combined(float ms);
        static int find_lsfg_callback(struct dl_phdr_info* info, size_t size, void* data);
        void* find_loaded_lsfg_lib();
        float current_fps();
        float min_frametime();
        float max_frametime();

};

extern "C" __attribute__((visibility("default"))) __attribute__((used))
void notify_extra_frame(uint64_t n_frames);

extern std::unique_ptr<lsfg_vk> lsfg_ptr;