#pragma once
#include <string>
#include "real_dlsym.h"
#include "spdlog/spdlog.h"
#include <vector>
#include "overlay_params.h"
#include <vulkan/vulkan_core.h>
class lsfg_vk {
    public:
        float real_fps;
        float fake_fps;
        void notify_extra_frame(uint64_t n_frames, VkSwapchainKHR swapchain, bool fake);
        void update_display_fps(uint64_t now);
        
        private:
        float frametimes[200] = {};
        float fake_frames[200] = {};
        uint64_t n_frames_since_update_real = 0;
        uint64_t n_frames_since_update_fake = 0;
        uint64_t n_frames_real = 0;
        uint64_t n_frames_fake = 0;
    };

extern "C" __attribute__((visibility("default"))) __attribute__((used))
void notify_extra_frame(uint64_t n_frames, VkSwapchainKHR swapchain, bool fake);

extern std::unique_ptr<lsfg_vk> lsfg_ptr;
