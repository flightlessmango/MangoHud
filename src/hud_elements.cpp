#include <spdlog/spdlog.h>
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>
#include "overlay.h"
#include "overlay_params.h"
#include "hud_elements.h"
#include "logging.h"
#include "battery.h"
#include "gamepad.h"
#include "cpu.h"
#include "gpu.h"
#include "memory.h"
#include "iostats.h"
#include "mesa/util/macros.h"
#include "string_utils.h"
#include "app/mangoapp.h"
#include <IconsForkAwesome.h>

#define CHAR_CELSIUS    "\xe2\x84\x83"
#define CHAR_FAHRENHEIT "\xe2\x84\x89"

using namespace std;

// Cut from https://github.com/ocornut/imgui/pull/2943
// Probably move to ImGui
float SRGBToLinear(float in)
{
    if (in <= 0.04045f)
        return in / 12.92f;
    else
        return powf((in + 0.055f) / 1.055f, 2.4f);
}

float LinearToSRGB(float in)
{
    if (in <= 0.0031308f)
        return in * 12.92f;
    else
        return 1.055f * powf(in, 1.0f / 2.4f) - 0.055f;
}

ImVec4 SRGBToLinear(ImVec4 col)
{
    col.x = SRGBToLinear(col.x);
    col.y = SRGBToLinear(col.y);
    col.z = SRGBToLinear(col.z);
    // Alpha component is already linear

    return col;
}

ImVec4 LinearToSRGB(ImVec4 col)
{
    col.x = LinearToSRGB(col.x);
    col.y = LinearToSRGB(col.y);
    col.z = LinearToSRGB(col.z);
    // Alpha component is already linear

    return col;
}

template<typename T, typename R = float>
R format_units(T value, const char*& unit)
{
    static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB"};
    size_t u = 0;
    R out_value = value;
    while (out_value > 1023 && u < ARRAY_SIZE(units)) {
        out_value /= 1024;
        ++u;
    }
    unit = units[u];
    return out_value;
}

void HudElements::convert_colors(const struct overlay_params& params)
{
    HUDElements.colors.update = false;
    auto convert = [](unsigned color) -> ImVec4 {
        ImVec4 fc = ImGui::ColorConvertU32ToFloat4(color);
        if (HUDElements.colors.convert)
            return SRGBToLinear(fc);
        return fc;
    };

    HUDElements.colors.cpu = convert(params.cpu_color);
    HUDElements.colors.gpu = convert(params.gpu_color);
    HUDElements.colors.vram = convert(params.vram_color);
    HUDElements.colors.ram = convert(params.ram_color);
    HUDElements.colors.engine = convert(params.engine_color);
    HUDElements.colors.io = convert(params.io_color);
    HUDElements.colors.frametime = convert(params.frametime_color);
    HUDElements.colors.background = convert(params.background_color);
    HUDElements.colors.text = convert(params.text_color);
    HUDElements.colors.media_player = convert(params.media_player_color);
    HUDElements.colors.wine = convert(params.wine_color);
    HUDElements.colors.battery = convert(params.battery_color);
    HUDElements.colors.gpu_load_low = convert(params.gpu_load_color[0]);
    HUDElements.colors.gpu_load_med = convert(params.gpu_load_color[1]);
    HUDElements.colors.gpu_load_high = convert(params.gpu_load_color[2]);
    HUDElements.colors.cpu_load_low = convert(params.cpu_load_color[0]);
    HUDElements.colors.cpu_load_med = convert(params.cpu_load_color[1]);
    HUDElements.colors.cpu_load_high = convert(params.cpu_load_color[2]);
    HUDElements.colors.fps_value_low = convert(params.fps_color[0]);
    HUDElements.colors.fps_value_med = convert(params.fps_color[1]);
    HUDElements.colors.fps_value_high = convert(params.fps_color[2]);

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_PlotLines] = convert(params.frametime_color);
    style.Colors[ImGuiCol_PlotHistogram] = convert(params.frametime_color);
    style.Colors[ImGuiCol_WindowBg]  = convert(params.background_color);
    style.Colors[ImGuiCol_Text] = convert(params.text_color);
    style.CellPadding.y = params.cellpadding_y * real_font_size.y;
    style.WindowRounding = params.round_corners;
}

void HudElements::convert_colors(bool do_conv, const struct overlay_params& params)
{
    HUDElements.colors.convert = do_conv;
    convert_colors(params);
}

/**
* Go to next column or second column on new row
*/
void ImguiNextColumnOrNewRow(int column = -1)
{
    if (column > -1 && column < ImGui::TableGetColumnCount())
        ImGui::TableSetColumnIndex(column);
    else
    {
        ImGui::TableNextColumn();
        if (ImGui::TableGetColumnIndex() == 0 && ImGui::TableGetColumnCount() > 1)
            ImGui::TableNextColumn();
    }
}

void ImGuiTableSetColumnIndex(int column)
{
    ImGui::TableSetColumnIndex(std::max(0, std::min(column, ImGui::TableGetColumnCount() - 1)));
}

void HudElements::time(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_time]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.00f), "%s", HUDElements.sw_stats->time.c_str());
    }
}

void HudElements::version(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_version]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::Text("%s", MANGOHUD_VERSION);
    }
}

void HudElements::gpu_stats(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        const char* gpu_text;
        if (HUDElements.params->gpu_text.empty())
            gpu_text = "GPU";
        else
            gpu_text = HUDElements.params->gpu_text.c_str();
        ImGui::TextColored(HUDElements.colors.gpu, "%s", gpu_text);
        ImGui::TableNextColumn();
        auto text_color = HUDElements.colors.text;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_load_change]){
            struct LOAD_DATA gpu_data = {
                HUDElements.colors.gpu_load_low,
                HUDElements.colors.gpu_load_med,
                HUDElements.colors.gpu_load_high,
                HUDElements.params->gpu_load_value[0],
                HUDElements.params->gpu_load_value[1]
            };

            auto load_color = change_on_load_temp(gpu_data, gpu_info.load);
            right_aligned_text(load_color, HUDElements.ralign_width, "%i", gpu_info.load);
            ImGui::SameLine(0, 1.0f);
            ImGui::TextColored(load_color,"%%");
        }
        else {
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.load);
            ImGui::SameLine(0, 1.0f);
            ImGui::TextColored(text_color,"%%");
            // ImGui::SameLine(150);
            // ImGui::Text("%s", "%");
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.temp);
            ImGui::SameLine(0, 1.0f);
            ImGui::Text("°C");
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.CoreClock);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_power]) {
            ImguiNextColumnOrNewRow();
#ifdef MANGOAPP
            right_aligned_text(text_color, HUDElements.ralign_width, "%.1f", gpu_info.powerUsage);
#else
            right_aligned_text(text_color, HUDElements.ralign_width, "%.0f", gpu_info.powerUsage);
#endif
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("W");
            ImGui::PopFont();
        }
    }
}

void HudElements::cpu_stats(){
    if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        const char* cpu_text;
        if (HUDElements.params->cpu_text.empty())
            cpu_text = "CPU";
        else
            cpu_text = HUDElements.params->cpu_text.c_str();

        ImGui::TextColored(HUDElements.colors.cpu, "%s", cpu_text);
        ImGui::TableNextColumn();
        auto text_color = HUDElements.colors.text;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_load_change]){
            int cpu_load_percent = int(cpuStats.GetCPUDataTotal().percent);
            struct LOAD_DATA cpu_data = {
                HUDElements.colors.cpu_load_low,
                HUDElements.colors.cpu_load_med,
                HUDElements.colors.cpu_load_high,
                HUDElements.params->cpu_load_value[0],
                HUDElements.params->cpu_load_value[1]
            };

            auto load_color = change_on_load_temp(cpu_data, cpu_load_percent);
            right_aligned_text(load_color, HUDElements.ralign_width, "%d", cpu_load_percent);
            ImGui::SameLine(0, 1.0f);
            ImGui::TextColored(load_color, "%%");
        }
        else {
            right_aligned_text(text_color, HUDElements.ralign_width, "%d", int(cpuStats.GetCPUDataTotal().percent));
            ImGui::SameLine(0, 1.0f);
            ImGui::Text("%%");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_temp]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuStats.GetCPUDataTotal().temp);
            ImGui::SameLine(0, 1.0f);
            ImGui::Text("°C");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_mhz]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuStats.GetCPUDataTotal().cpu_mhz);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_power]){
            ImguiNextColumnOrNewRow();
        #ifdef MANGOAPP
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", cpuStats.GetCPUDataTotal().power);
        #else
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", cpuStats.GetCPUDataTotal().power);
        #endif
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("W");
            ImGui::PopFont();
        }
    }
}

void HudElements::core_load(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_core_load]){
         for (const CPUData &cpuData : cpuStats.GetCPUData())
         {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextColored(HUDElements.colors.cpu, "CPU");
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::TextColored(HUDElements.colors.cpu,"%i", cpuData.cpu_id);
            ImGui::PopFont();
            ImGui::TableNextColumn();
            auto text_color = HUDElements.colors.text;
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_core_load_change]){
                int cpu_load_percent = int(cpuData.percent);
                struct LOAD_DATA cpu_data = {
                    HUDElements.colors.cpu_load_low,
                    HUDElements.colors.cpu_load_med,
                    HUDElements.colors.cpu_load_high,
                    HUDElements.params->cpu_load_value[0],
                    HUDElements.params->cpu_load_value[1]
                };
                auto load_color = change_on_load_temp(cpu_data, cpu_load_percent);
                right_aligned_text(load_color, HUDElements.ralign_width, "%d", cpu_load_percent);
                ImGui::SameLine(0, 1.0f);
                ImGui::TextColored(load_color, "%%");
                ImguiNextColumnOrNewRow();
            }
            else {
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", int(cpuData.percent));
                ImGui::SameLine(0, 1.0f);
                ImGui::Text("%%");
                ImguiNextColumnOrNewRow();
            }
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuData.mhz);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
         }
    }
}

void HudElements::io_stats(){
#ifndef _WIN32
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] || HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
            ImGui::TextColored(HUDElements.colors.io, "IO RD");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
            ImGui::TextColored(HUDElements.colors.io, "IO RW");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read])
            ImGui::TextColored(HUDElements.colors.io, "IO WR");

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read]){
            ImGui::TableNextColumn();
            const float val = g_io_stats.per_second.read;
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MiB/s");
            ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
            ImguiNextColumnOrNewRow();
            const float val = g_io_stats.per_second.write;
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MiB/s");
            ImGui::PopFont();
        }
    }
#endif
}

void HudElements::vram(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vram]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(HUDElements.colors.vram, "VRAM");
        ImGui::TableNextColumn();
        // Add gtt_used to vram usage for APUs
        if (cpuStats.cpu_type == "APU")
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", gpu_info.memoryUsed + gpu_info.gtt_used);
        else
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", gpu_info.memoryUsed);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("GiB");
        ImGui::PopFont();
#ifndef MANGOAPP
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_clock]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", gpu_info.MemClock);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
        }
#endif
    }
}

void HudElements::ram(){
#ifdef __linux__
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(HUDElements.colors.ram, "RAM");
        ImGui::TableNextColumn();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", memused);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("GiB");
        ImGui::PopFont();
    }

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_swap]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", swapused);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("GiB");
        ImGui::PopFont();
    }
#endif
}

void HudElements::procmem()
{
#ifdef __linux__
    const char* unit = nullptr;
    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem])
        return;

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(HUDElements.colors.ram, "PMEM");
    ImGui::TableNextColumn();
    right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.resident, unit));
    ImGui::SameLine(0,1.0f);
    ImGui::PushFont(HUDElements.sw_stats->font1);
    ImGui::Text("%s", unit);
    ImGui::PopFont();

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem_shared]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.shared, unit));
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("%s", unit);
        ImGui::PopFont();
    }

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem_virt]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.virt, unit));
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("%s", unit);
        ImGui::PopFont();
    }
#endif
}

void HudElements::fps(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_only]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(HUDElements.colors.engine, "%s", engines[HUDElements.sw_stats->engine]);
        ImGui::TableNextColumn();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_color_change]){
            int fps = int(HUDElements.sw_stats->fps);
            struct LOAD_DATA fps_data = {
            HUDElements.colors.fps_value_low,
            HUDElements.colors.fps_value_med,
            HUDElements.colors.fps_value_high,
            HUDElements.params->fps_value[0],
            HUDElements.params->fps_value[1]
            };
            auto load_color = change_on_load_temp(fps_data, fps);
            right_aligned_text(load_color, HUDElements.ralign_width, "%.0f", HUDElements.sw_stats->fps);
        }
        else {
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", HUDElements.sw_stats->fps);
        }
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("FPS");
        ImGui::PopFont();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frametime]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", 1000 / HUDElements.sw_stats->fps);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("ms");
            ImGui::PopFont();
        }
    } else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(HUDElements.colors.engine, "%s", HUDElements.sw_stats->engineName.c_str());
    }
}

void HudElements::fps_only(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_only]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        auto load_color = HUDElements.colors.text;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_color_change]){
            int fps = int(HUDElements.sw_stats->fps);
            struct LOAD_DATA fps_data = {
            HUDElements.colors.fps_value_low,
            HUDElements.colors.fps_value_med,
            HUDElements.colors.fps_value_high,
            HUDElements.params->fps_value[0],
            HUDElements.params->fps_value[1]
            };
            load_color = change_on_load_temp(fps_data, fps);
        }
        ImGui::TextColored(load_color, "%.0f", HUDElements.sw_stats->fps);
    }
}

void HudElements::gpu_name(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_name] && !HUDElements.sw_stats->gpuName.empty()){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine,
            "%s", HUDElements.sw_stats->gpuName.c_str());
        ImGui::PopFont();
    }
}

void HudElements::engine_version(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        if (HUDElements.is_vulkan) {
            if ((HUDElements.sw_stats->engine == EngineTypes::DXVK || HUDElements.sw_stats->engine == EngineTypes::VKD3D)){
                ImGui::TextColored(HUDElements.colors.engine,
                    "%s/%d.%d.%d", HUDElements.sw_stats->engineVersion.c_str(),
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            } else {
                ImGui::TextColored(HUDElements.colors.engine,
                    "%d.%d.%d",
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            }
        } else {
            ImGui::TextColored(HUDElements.colors.engine,
                "%d.%d%s", HUDElements.sw_stats->version_gl.major, HUDElements.sw_stats->version_gl.minor,
                HUDElements.sw_stats->version_gl.is_gles ? " ES" : "");
        }
        ImGui::PopFont();
    }
}

void HudElements::vulkan_driver(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vulkan_driver] && !HUDElements.sw_stats->driverName.empty()){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine,
            "%s", HUDElements.sw_stats->driverName.c_str());
        ImGui::PopFont();
    }
}

void HudElements::arch(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_arch]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s", "" MANGOHUD_ARCH);
        ImGui::PopFont();
    }
}

void HudElements::wine(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_wine]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        if (!wineVersion.empty()){
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::TextColored(HUDElements.colors.wine, "%s", wineVersion.c_str());
            ImGui::PopFont();
        }
    }
}

void HudElements::frame_timing(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_timing]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s", "Frametime");
        ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
        ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
        right_aligned_text(HUDElements.colors.text, ImGui::GetContentRegionAvail().x, "min: %.1fms, max: %.1fms", min_frametime, max_frametime);
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        char hash[40];
        snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_frame_timing]);
        HUDElements.sw_stats->stat_selector = OVERLAY_PLOTS_frame_timing;
        HUDElements.sw_stats->time_dividor = 1000000.0f; /* ns -> ms */
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        double min_time = 0.0f;
        double max_time = 50.0f;
#ifdef MANGOAPP
        const ImVec2 sz = ImGui::CalcTextSize("1000.0ms");
        float width = ImGui::GetWindowContentRegionWidth() - sz.x;
#else
        float width = ImGui::GetWindowContentRegionWidth();
#endif
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_histogram]){
            ImGui::PlotHistogram(hash, get_time_stat, HUDElements.sw_stats,
                                ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                                NULL, min_time, max_time,
                                ImVec2(width, 50));
        } else {
            ImGui::PlotLines(hash, get_time_stat, HUDElements.sw_stats,
                            ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                            NULL, min_time, max_time,
                            ImVec2(width, 50));
        }
#ifdef MANGOAPP
        ImGui::SameLine();
        ImGuiTableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
        right_aligned_text(HUDElements.colors.text, ImGui::GetContentRegionAvail().x, "%.1fms", frametime / 1000.f);
#endif
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
}

void HudElements::media_player(){
#ifdef HAVE_DBUS
    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_media_player])
        return;

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    uint32_t f_idx = (HUDElements.sw_stats->n_frames - 1) % ARRAY_SIZE(HUDElements.sw_stats->frames_stats);
    uint64_t frame_timing = HUDElements.sw_stats->frames_stats[f_idx].stats[OVERLAY_PLOTS_frame_timing];
    ImFont scaled_font = *HUDElements.sw_stats->font_text;
    scaled_font.Scale = HUDElements.params->font_scale_media_player;
    ImGui::PushFont(&scaled_font);
    {
        std::unique_lock<std::mutex> lck(main_metadata.mtx, std::try_to_lock);
        if (lck.owns_lock())
            render_mpris_metadata(*HUDElements.params, main_metadata, frame_timing);
        else
            SPDLOG_DEBUG("failed to acquire lock");
    }
    ImGui::PopFont();
#endif
}

void HudElements::resolution(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_resolution]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        const auto res  = ImGui::GetIO().DisplaySize;
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "Resolution");
        ImGuiTableSetColumnIndex(HUDElements.text_column);
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "%.0fx%.0f", res.x, res.y);
        ImGui::PopFont();
    }
}

void HudElements::show_fps_limit(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_show_fps_limit]){
        int fps = 0;
        if (fps_limit_stats.targetFrameTime.count())
            fps = 1000000000 / fps_limit_stats.targetFrameTime.count();
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s","FPS limit");
        ImGuiTableSetColumnIndex(HUDElements.text_column);
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", fps);
        ImGui::PopFont();
    }
}

void HudElements::custom_text_center(){
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::PushFont(HUDElements.sw_stats->font1);
    const std::string& value = HUDElements.ordered_functions[HUDElements.place].second;
    center_text(value);
    ImGui::TextColored(HUDElements.colors.text, "%s",value.c_str());
    ImGui::NewLine();
    ImGui::PopFont();
}

void HudElements::custom_text(){
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::PushFont(HUDElements.sw_stats->font1);
    const std::string& value = HUDElements.ordered_functions[HUDElements.place].second;
    ImGui::TextColored(HUDElements.colors.text, "%s",value.c_str());
    ImGui::PopFont();
}

void HudElements::_exec(){
    //const std::string& value = HUDElements.ordered_functions[HUDElements.place].second;
    ImGui::PushFont(HUDElements.sw_stats->font1);
    ImGui::TableNextColumn();
    for (auto& item : HUDElements.exec_list){
        if (item.pos == HUDElements.place)
            ImGui::Text("%s", item.ret.c_str());
    }
    ImGui::PopFont();
}

void HudElements::gamemode(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gamemode]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s", "GAMEMODE");
        ImGuiTableSetColumnIndex(HUDElements.text_column);
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.gamemode_bol ? "ON" : "OFF");
        ImGui::PopFont();
    }
}

void HudElements::vkbasalt(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vkbasalt]){
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s", "VKBASALT");
        ImGuiTableSetColumnIndex(HUDElements.text_column);
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.vkbasalt_bol ? "ON" : "OFF");
        ImGui::PopFont();
    }
}

void HudElements::battery(){
#ifdef __linux__
    if (Battery_Stats.batt_count > 0) {
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery]) {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextColored(HUDElements.colors.battery, "BATT");
            ImGui::TableNextColumn();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_icon]) {
                switch(int(Battery_Stats.current_percent)){
                    case 0 ... 33:
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_QUARTER);
                        break;
                    case 34 ... 66:
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_HALF);
                        break;
                    case 67 ... 97:
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_THREE_QUARTERS);
                        break;
                    case 98 ... 100:
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_FULL);
                        break;
                }
            }
            else {
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", Battery_Stats.current_percent);
                ImGui::SameLine(0,1.0f);
                ImGui::Text("%%");
            }
            if (Battery_Stats.current_watt != 0) {
                ImguiNextColumnOrNewRow();
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", Battery_Stats.current_watt);
                ImGui::SameLine(0,1.0f);
                ImGui::PushFont(HUDElements.sw_stats->font1);
                ImGui::Text("W");
                ImGui::PopFont();
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                float hours;
                float minutes;
                // float seconds;
                minutes = std::modf(Battery_Stats.remaining_time, &hours);
                minutes *= 60;
                // seconds = std::modf(minutes, &minutes);
                // seconds *= 60;
                ImGui::PushFont(HUDElements.sw_stats->font1);
                ImGui::TextColored(HUDElements.colors.text, "%s", "Remaining Time");
                ImGui::PopFont();
                ImGuiTableSetColumnIndex(2);
                // ImGui::TextColored(HUDElements.colors.text, "%02.0f:%02.0f:%02.0f", hours, minutes, seconds);
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%02.0f:%02.0f", hours, minutes);
            }
            else {
                ImguiNextColumnOrNewRow();
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_PLUG);
            }
        }
    }
#endif
}

void HudElements::gamescope_fsr(){
#ifdef MANGOAPP
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fsr]) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        string FSR_TEXT;
        ImVec4 FSR_COLOR;
        if (g_fsrUpscale){
            FSR_TEXT = "ON";
            FSR_COLOR = HUDElements.colors.fps_value_high;
        } else {
            FSR_TEXT = "OFF";
            FSR_COLOR = HUDElements.colors.fps_value_low;
        }

        ImGui::TextColored(HUDElements.colors.engine, "%s", "FSR");
        ImGui::TableNextColumn();
        right_aligned_text(FSR_COLOR, HUDElements.ralign_width, "%s", FSR_TEXT.c_str());
        if (g_fsrUpscale){
            if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hide_fsr_sharpness]) {
                ImguiNextColumnOrNewRow();
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", g_fsrSharpness);
                ImGui::SameLine(0,1.0f);
                ImGui::PushFont(HUDElements.sw_stats->font1);
                ImGui::Text("Sharp");
                ImGui::PopFont();
            }
        }
    }
#endif
}

void HudElements::gamescope_frame_timing(){
#ifdef MANGOAPP
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_debug]) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "%s", "App");
        for (size_t i = 0; i < HUDElements.params->table_columns - 1; i++)
            ImGui::TableNextColumn();
        ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
        auto min = std::min_element(gamescope_debug_app.begin(), gamescope_debug_app.end());
        auto max = std::max_element(gamescope_debug_app.begin(), gamescope_debug_app.end());
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "min: %.1fms, max: %.1fms", min[0], max[0]);
        ImGui::PopFont();
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        char hash[40];
        snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_frame_timing]);
        HUDElements.sw_stats->stat_selector = OVERLAY_PLOTS_frame_timing;
        HUDElements.sw_stats->time_dividor = 1000000.0f; /* ns -> ms */
        double min_time = 0.0f;
        double max_time = 50.0f;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PlotLines("", gamescope_debug_app.data(),
                gamescope_debug_app.size(), 0,
                NULL, min_time, max_time,
                    ImVec2(ImGui::GetContentRegionAvailWidth() * HUDElements.params->table_columns - 30, 50));
            ImGui::SameLine();
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("%.1fms", gamescope_debug_app.back());
            ImGui::PopFont();
        ImGui::PopStyleColor();
        if (gamescope_debug_latency.back() > -1){
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::TextColored(HUDElements.colors.engine, "%s", "Latency");
            for (size_t i = 0; i < HUDElements.params->table_columns - 1; i++)
                ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            min = std::min_element(gamescope_debug_latency.begin(), gamescope_debug_latency.end());
            max = std::max_element(gamescope_debug_latency.begin(), gamescope_debug_latency.end());
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "min: %.1fms, max: %.1fms", min[0], max[0]);
            ImGui::PopFont();
            ImGui::TableNextRow(); ImGui::TableNextColumn();

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0,0,1,1));
            ImGui::PlotLines("", gamescope_debug_latency.data(),
                    gamescope_debug_latency.size(), 0,
                    NULL, min_time, max_time,
                    ImVec2(ImGui::GetContentRegionAvailWidth() * HUDElements.params->table_columns - 30, 50));
            ImGui::SameLine();
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("%.1fms", gamescope_debug_latency.back());
            ImGui::PopFont();
            ImGui::PopStyleColor(2);
        }
    }
#endif
}

void HudElements::gamepad_battery()
{
#ifdef __linux__
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gamepad_battery]) {
        if (gamepad_found) {
            for (int i = 0; i < gamepad_count; i++) {
                std::string battery = gamepad_data[i].battery;
                std::string name = gamepad_data[i].name;
                std::string battery_percent = gamepad_data[i].battery_percent;
                bool report_percent = gamepad_data[i].report_percent;
                bool charging = gamepad_data[i].is_charging;

                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::PushFont(HUDElements.sw_stats->font1);
                ImGui::TextColored(HUDElements.colors.engine, "%s", name.c_str());
                ImGui::TableNextColumn();
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gamepad_battery_icon]) {
                    if (charging)
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_USB);
                    else {
                        if (battery == "Full")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_FULL);
                        else if (battery == "High")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_THREE_QUARTERS);
                        else if (battery == "Normal")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_HALF);
                        else if (battery == "Low")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_BATTERY_QUARTER);
                        else if (battery == "Unknown")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_USB);
                    }
                }
                else {
                    if (charging)
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_USB);
                    else if (report_percent) {
                        right_aligned_text(HUDElements.colors.text,HUDElements.ralign_width, "%s", battery_percent.c_str());
                        ImGui::SameLine(0,1.0f);
                        ImGui::Text("%%");
                    }
                    else {
                        if (battery == "Unknown")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_USB);
                        else
                            right_aligned_text(HUDElements.colors.text,HUDElements.ralign_width, "%s", battery.c_str());
                    }
                }
                ImGui::PopFont();
            }
        }
    }
#endif
}

void HudElements::frame_count(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_count]){
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.colors.engine, "Frame Count");
        ImGuiTableSetColumnIndex(HUDElements.text_column);
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%" PRIu64, HUDElements.sw_stats->n_frames);
        ImGui::PopFont();
    }
}

void HudElements::fan(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fan] && fan_speed != -1) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(HUDElements.colors.engine, "%s", "FAN");
        ImGui::TableNextColumn();
        right_aligned_text(HUDElements.colors.text,HUDElements.ralign_width, "%i", fan_speed);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("RPM");
        ImGui::PopFont();
    }
}

void HudElements::throttling_status(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_throttling_status]){
        if (gpu_info.is_power_throttled || gpu_info.is_current_throttled || gpu_info.is_temp_throttled || gpu_info.is_other_throttled){
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextColored(HUDElements.colors.engine, "%s", "Throttling");
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            if (gpu_info.is_power_throttled)
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "Power");
            if (gpu_info.is_current_throttled)
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "Current");
            if (gpu_info.is_temp_throttled)
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "Temp");
            if (gpu_info.is_other_throttled)
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "Other");
        }
    }
}

void HudElements::graphs(){
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
    const std::string& value = HUDElements.ordered_functions[HUDElements.place].second;
    assert(kMaxGraphEntries >= graph_data.size());
    std::vector<float> arr(kMaxGraphEntries - graph_data.size());

    ImGui::PushFont(HUDElements.sw_stats->font1);
    if (value == "cpu_load"){
        for (auto& it : graph_data){
            arr.push_back(float(it.cpu_load));
        }
        HUDElements.max = 100;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "CPU Load");
    }

    if (value == "gpu_load"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_load));
        }
        HUDElements.max = 100;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "GPU Load");
    }

    if (value == "cpu_temp"){
        for (auto& it : graph_data){
            arr.push_back(float(it.cpu_temp));
        }
        if (int(arr.back()) > HUDElements.cpu_temp_max)
            HUDElements.cpu_temp_max = arr.back();

        HUDElements.max = HUDElements.cpu_temp_max;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "CPU Temp");
    }

    if (value == "gpu_temp"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_temp));
        }
        if (int(arr.back()) > HUDElements.gpu_temp_max)
            HUDElements.gpu_temp_max = arr.back();

        HUDElements.max = HUDElements.gpu_temp_max;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "GPU Temp");
    }

    if (value == "gpu_core_clock"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_core_clock));
        }
        if (int(arr.back()) > HUDElements.gpu_core_max)
            HUDElements.gpu_core_max = arr.back();

        HUDElements.max = HUDElements.gpu_core_max;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "GPU Core Clock");
    }

    if (value == "gpu_mem_clock"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_mem_clock));
        }
        if (int(arr.back()) > HUDElements.gpu_mem_max)
            HUDElements.gpu_mem_max = arr.back();

        HUDElements.max = HUDElements.gpu_mem_max;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "GPU Mem Clock");
    }

    if (value == "vram"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_vram_used));
        }

        HUDElements.max = gpu_info.memoryTotal;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "VRAM");
    }
#ifdef __linux__
    if (value == "ram"){
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram])
            HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram] = true;
        for (auto& it : graph_data){
            arr.push_back(float(it.ram_used));
        }

        HUDElements.max = memmax;
        HUDElements.min = 0;
        ImGui::TextColored(HUDElements.colors.engine, "%s", "RAM");
    }
#endif
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f,5.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_histogram]){
        ImGui::PlotLines("", arr.data(),
                arr.size(), 0,
                NULL, HUDElements.min, HUDElements.max,
                ImVec2(ImGui::GetWindowContentRegionWidth(), 50));
    } else {
        ImGui::PlotHistogram("", arr.data(),
            arr.size(), 0,
            NULL, HUDElements.min, HUDElements.max,
            ImVec2(ImGui::GetWindowContentRegionWidth(), 50));
    }
    ImGui::Dummy(ImVec2(0.0f,5.0f));
    ImGui::PopStyleColor(1);
}

void HudElements::sort_elements(const std::pair<std::string, std::string>& option){
    const auto& param = option.first;
    const auto& value = option.second;

    // Use this to always add to front of vector
    //ordered_functions.insert(ordered_functions.begin(),std::make_pair(param,value));

    if (param == "version")         { ordered_functions.push_back({version, value});                }
    if (param == "time")            { ordered_functions.push_back({time, value});                   }
    if (param == "gpu_stats")       { ordered_functions.push_back({gpu_stats, value});              }
    if (param == "cpu_stats")       { ordered_functions.push_back({cpu_stats, value});              }
    if (param == "core_load")       { ordered_functions.push_back({core_load, value});              }
#ifndef MANGOAPP
    if (param == "io_stats")        { ordered_functions.push_back({io_stats, value});               }
    if (param == "arch")            { ordered_functions.push_back({arch, value});                   }
    if (param == "wine")            { ordered_functions.push_back({wine, value});                   }
    if (param == "procmem")         { ordered_functions.push_back({procmem, value});                }
    if (param == "gamemode")        { ordered_functions.push_back({gamemode, value});               }
    if (param == "vkbasalt")        { ordered_functions.push_back({vkbasalt, value});               }
    if (param == "engine_version")  { ordered_functions.push_back({engine_version, value});         }
    if (param == "vulkan_driver")   { ordered_functions.push_back({vulkan_driver, value});          }
    if (param == "resolution")      { ordered_functions.push_back({resolution, value});             }
    if (param == "show_fps_limit")  { ordered_functions.push_back({show_fps_limit, value});         }
#endif
    if (param == "vram")            { ordered_functions.push_back({vram, value});                   }
    if (param == "ram")             { ordered_functions.push_back({ram, value});                    }
    if (param == "fps")             { ordered_functions.push_back({fps, value});                    }
    if (param == "gpu_name")        { ordered_functions.push_back({gpu_name, value});               }
    if (param == "frame_timing")    { ordered_functions.push_back({frame_timing, value});           }
    if (param == "media_player")    { ordered_functions.push_back({media_player, value});           }
    if (param == "custom_text")     { ordered_functions.push_back({custom_text, value});            }
    if (param == "custom_text_center")  { ordered_functions.push_back({custom_text_center, value}); }
    if (param == "exec")            { ordered_functions.push_back({_exec, value});
                                      exec_list.push_back({int(ordered_functions.size() - 1), value});       }
    if (param == "battery")         { ordered_functions.push_back({battery, value});                }
    if (param == "fps_only")        { ordered_functions.push_back({fps_only, value});               }
    if (param == "fsr")             { ordered_functions.push_back({gamescope_fsr, value});          }
    if (param == "debug")           { ordered_functions.push_back({gamescope_frame_timing, value}); }
    if (param == "gamepad_battery") { ordered_functions.push_back({gamepad_battery, value});        }
    if (param == "frame_count")     { ordered_functions.push_back({frame_count, value});            }
    if (param == "fan")             { ordered_functions.push_back({fan, value});                    }
    if (param == "throttling_status")        { ordered_functions.push_back({throttling_status, value});               }
    if (param == "graphs"){
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_graphs])
            HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_graphs] = true;
        auto values = str_tokenize(value);
        for (auto& value : values) {
            if (find(permitted_params.begin(), permitted_params.end(), value) != permitted_params.end())
                ordered_functions.push_back({graphs, value});
            else
            {
                spdlog::error("Unrecognized graph type: {}", value);
            }
        }
    }
    return;
}

void HudElements::legacy_elements(){
    string value = "NULL";
    ordered_functions.clear();
    ordered_functions.push_back({time,               value});
    ordered_functions.push_back({version,            value});
    ordered_functions.push_back({gpu_stats,          value});
    ordered_functions.push_back({cpu_stats,          value});
    ordered_functions.push_back({core_load,          value});
#ifndef MANGOAPP
    ordered_functions.push_back({io_stats,           value});
#endif
    ordered_functions.push_back({vram,               value});
    ordered_functions.push_back({ram,                value});
    ordered_functions.push_back({battery,            value});
    ordered_functions.push_back({fan,                value});
    ordered_functions.push_back({gamescope_fsr,      value});
    ordered_functions.push_back({throttling_status,  value});
    ordered_functions.push_back({fps,                value});
    ordered_functions.push_back({fps_only,           value});
#ifndef MANGOAPP
    ordered_functions.push_back({engine_version,     value});
#endif
    ordered_functions.push_back({gpu_name,           value});
#ifndef MANGOAPP
    ordered_functions.push_back({vulkan_driver,      value});
    ordered_functions.push_back({arch,               value});
    ordered_functions.push_back({wine,               value});
#endif
    ordered_functions.push_back({frame_timing,       value});
    ordered_functions.push_back({frame_count,         value});
    ordered_functions.push_back({gamescope_frame_timing, value});
#ifndef MANGOAPP
    ordered_functions.push_back({gamemode,           value});
    ordered_functions.push_back({vkbasalt,           value});
    ordered_functions.push_back({show_fps_limit,     value});
    ordered_functions.push_back({resolution,         value});
#endif
    ordered_functions.push_back({gamepad_battery,    value});
    ordered_functions.push_back({media_player,       value});
}

void HudElements::update_exec(){
    for(auto& item : exec_list)
        item.ret = exec(item.value);
}

HudElements HUDElements;
