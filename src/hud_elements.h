#pragma once
#include <vector>
#include <string>
#include <utility>
#include <imgui.h>
#include "timing.hpp"
#include <functional>
#include "winesync.h"
#include "vulkan/vulkan.h"
#include <array>
#include "net.h"
#include "overlay_params.h"
#include "shell.h"

struct Function {
    std::function<void()> run;  // Using std::function instead of a raw function pointer for more flexibility
    std::string name;
    std::string value;
};

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
        bool is_vulkan = true, gamemode_bol = false, vkbasalt_bol = false;
        int place;
        int text_column = 1;
        int table_columns_count = 0;
        int g_fsrUpscale = -1;
        int g_fsrSharpness = -1;
        Clock::time_point last_exec;
        std::vector<std::pair<std::string, std::string>> options;
        std::vector<Function> ordered_functions;
        std::vector<float> gamescope_debug_latency {};
        std::vector<float> gamescope_debug_app {};
        int min, max, gpu_core_max, gpu_mem_max, cpu_temp_max, gpu_temp_max;
        const std::vector<std::string> permitted_params = {
            "gpu_load", "cpu_load", "gpu_core_clock", "gpu_mem_clock",
            "vram", "ram", "cpu_temp", "gpu_temp"
        };
        std::vector<exec_entry> exec_list;
        std::chrono::steady_clock::time_point overlay_start = std::chrono::steady_clock::now();
        uint32_t vendorID;
        int hdr_status = 0;
        int refresh = 0;
        unsigned int vsync = 10;
        std::unique_ptr<WineSync> winesync_ptr = nullptr;
        std::unique_ptr<Net> net = nullptr;
#ifdef __linux__
        std::unique_ptr<Shell> shell = nullptr;
#endif

        void sort_elements(const std::pair<std::string, std::string>& option);
        void legacy_elements();
        void update_exec();
        int convert_to_fahrenheit(int celsius);
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
        static void fps_only();
        static void gamescope_fsr();
        static void gamescope_frame_timing();
        static void device_battery();
        static void frame_count();
        static void fan();
        static void throttling_status();
        static void exec_name();
        static void duration();
        static void fps_metrics();
        static void hdr();
        static void refresh_rate();
        static void winesync();
        static void present_mode();
        static void network();

        void convert_colors(const struct overlay_params& params);
        void convert_colors(bool do_conv, const struct overlay_params& params);
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
                fps_value_high,
                text_outline,
                network;
        } colors {};

        void TextColored(ImVec4 col, const char *fmt, ...);

        std::array<VkPresentModeKHR, 6> presentModes = {
            VK_PRESENT_MODE_FIFO_RELAXED_KHR,
            VK_PRESENT_MODE_IMMEDIATE_KHR,
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
            VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,
            VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR};

        std::map<VkPresentModeKHR, std::string> presentModeMap = {
            {VK_PRESENT_MODE_IMMEDIATE_KHR, "IMMEDIATE"},
            {VK_PRESENT_MODE_MAILBOX_KHR, "MAILBOX"},
            {VK_PRESENT_MODE_FIFO_KHR, "FIFO"},
            {VK_PRESENT_MODE_FIFO_RELAXED_KHR, "FIFO Relaxed"},
            {VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR, "DEMAND"},
            {VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, "CONTINUOUS"}
        };

        VkPresentModeKHR cur_present_mode;

        std::string get_present_mode(){
            if (is_vulkan)
                return presentModeMap[cur_present_mode];
            else
                return vsync == 0 ? "OFF" : "ON";

        }
};

extern HudElements HUDElements;
