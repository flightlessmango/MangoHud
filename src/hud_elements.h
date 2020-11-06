#pragma once
#include "overlay.h"
#include "overlay_params.h"
#include <functional>
#include <map>
#include <sstream>

class HudElements{
    public:
        struct swapchain_stats *sw_stats;
        struct overlay_params *params;
        float ralign_width;
        float old_scale;
        bool is_vulkan;
        int place;
        std::vector<std::pair<std::string, std::string>> options;
        std::vector<std::pair<void(*)(), std::string >> ordered_functions;
        int min, max, gpu_core_max, gpu_mem_max;
        void sort_elements(std::pair<std::string, std::string> option);
        static void version();
        static void time();
        static void gpu_stats();
        static void cpu_stats();
        static void core_load();
        static void io_stats();
        static void vram();
        static void ram();
        static void fps();
        static void engine_version();
        static void gpu_name();
        static void vulkan_driver();
        static void arch();
        static void wine();
        static void frame_timing();
        static void media_player();
        static void graphs();
};

extern HudElements HUDElements;
