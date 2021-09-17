#pragma once
#include <vector>
#include <string>
#include <utility>
#include <imgui.h>
#include "timing.hpp"

struct overlay_params;
class HudElements{
    public:
        struct swapchain_stats *sw_stats;
        struct overlay_params *params;
        struct exec_entry {
            int             pos;
            std::string     value;
            std::string     ret;
        };
        float ralign_width;
        float old_scale;
        float res_width, res_height;
        bool is_vulkan, gamemode_bol = false, vkbasalt_bol = false;
        int place;
        Clock::time_point last_exec;
        std::vector<std::pair<std::string, std::string>> options;
        std::vector<std::pair<void(*)(), std::string >> ordered_functions;
        float min, max;
        int gpu_core_max, gpu_mem_max, cpu_temp_max, gpu_temp_max;
        const std::vector<std::string> permitted_params = {
            "gpu_load", "cpu_load", "gpu_core_clock", "gpu_mem_clock",
            "vram", "ram", "cpu_temp", "gpu_temp"
        };
        std::vector<exec_entry> exec_list;
        void sort_elements(const std::pair<std::string, std::string>& option);
        void legacy_elements();
        void update_exec();
        static void version();
        static void time();
        static void gpu_stats();
        static void cpu_stats();
        static void core_load();
        static void io_stats();
        static void vram();
        static void ram();
        static void procmem();
        static void fps();
        static void engine_version();
        static void gpu_name();
        static void vulkan_driver();
        static void arch();
        static void wine();
        static void frame_timing();
        static void media_player();
        static void resolution();
        static void show_fps_limit();
        static void custom_text_center();
        static void custom_text();
        static void vkbasalt();
        static void gamemode();
        static void graphs();
        static void _exec();
        static void battery();

        void convert_colors(struct overlay_params& params);
        void convert_colors(bool do_conv, struct overlay_params& params);
        struct hud_colors {
            bool convert, update;
            ImVec4 cpu,
                gpu,
                vram,
                ram,
                swap,
                engine,
                io,
                frametime,
                background,
                text,
                media_player,
                wine,
                battery,
                gpu_load_low,
                gpu_load_med,
                gpu_load_high,
                cpu_load_low,
                cpu_load_med,
                cpu_load_high,
                fps_value_low,
                fps_value_med,
                fps_value_high;
        } colors {};

};

extern HudElements HUDElements;
