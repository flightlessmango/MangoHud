#include <spdlog/spdlog.h>
#include <algorithm>
#include <functional>
#include <sstream>
#include <cmath>
#include <map>
#include "overlay.h"
#include "overlay_params.h"
#include "hud_elements.h"
#include "logging.h"
#include "battery.h"
#include "device.h"
#include "cpu.h"
#include "gpu.h"
#include "memory.h"
#include "iostats.h"
#include "mesa/util/macros.h"
#include "string_utils.h"
#include "app/mangoapp.h"
#include <IconsForkAwesome.h>
#include "version.h"
#include "blacklist.h"
#ifdef __linux__
#include "implot.h"
#endif
#include "amdgpu.h"
#include "fps_metrics.h"

#define CHAR_CELSIUS    "\xe2\x84\x83"
#define CHAR_FAHRENHEIT "\xe2\x84\x89"

using namespace std;

// Cut from https://github.com/ocornut/imgui/pull/2943
// Probably move to ImGui
static float SRGBToLinear(float in)
{
    if (in <= 0.04045f)
        return in / 12.92f;
    else
        return powf((in + 0.055f) / 1.055f, 2.4f);
}

static ImVec4 SRGBToLinear(ImVec4 col)
{
    col.x = SRGBToLinear(col.x);
    col.y = SRGBToLinear(col.y);
    col.z = SRGBToLinear(col.z);
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
    auto convert = [&params](unsigned color) -> ImVec4 {
        ImVec4 fc = ImGui::ColorConvertU32ToFloat4(color);
        fc.w = params.alpha;
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
    HUDElements.colors.text_outline = convert(params.text_outline_color);
    HUDElements.colors.network = convert(params.network_color);

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

void HudElements::TextColored(ImVec4 col, const char *fmt, ...){
    auto textColor = ImGui::ColorConvertFloat4ToU32(col);
    char buffer[128] {};

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    RenderOutlinedText(buffer, textColor);
}

int HudElements::convert_to_fahrenheit(int celsius){
    int fahrenheit = (celsius * 9 / 5) + 32;
    return fahrenheit;
}

static void ImguiNextColumnFirstItem()
{
    ImGui::TableNextColumn();
    HUDElements.table_columns_count += 1;
}
/**
* Go to next column or second column on new row
*/
static void ImguiNextColumnOrNewRow(int column = -1)
{
    if (column > -1 && column < ImGui::TableGetColumnCount())
        ImGui::TableSetColumnIndex(column);
    else
    {
        ImGui::TableNextColumn();
        HUDElements.table_columns_count += 1;
        if (ImGui::TableGetColumnIndex() == 0 && ImGui::TableGetColumnCount() > 1) {
            ImGui::TableNextColumn();
            HUDElements.table_columns_count += 1;
        }
    }
}

static void ImGuiTableSetColumnIndex(int column)
{
    ImGui::TableSetColumnIndex(std::max(0, std::min(column, ImGui::TableGetColumnCount() - 1)));
}

void HudElements::time(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_time]){
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal] &&
            !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] &&
            !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_time_no_label]){
            ImguiNextColumnFirstItem();
            HUDElements.TextColored(HUDElements.colors.text, "Time");
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.sw_stats->time.c_str());
        } else {
            ImguiNextColumnFirstItem();
            HUDElements.TextColored(HUDElements.colors.text, "%s", HUDElements.sw_stats->time.c_str());
        }
    }
}

void HudElements::version(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_version]){
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.text, "%s", MANGOHUD_VERSION);
    }
}

void HudElements::gpu_stats(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats]){
        ImguiNextColumnFirstItem();
        const char* gpu_text;
        if (HUDElements.params->gpu_text.empty())
            gpu_text = "GPU";
        else
            gpu_text = HUDElements.params->gpu_text.c_str();
        HUDElements.TextColored(HUDElements.colors.gpu, "%s", gpu_text);        ImguiNextColumnOrNewRow();
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
            HUDElements.TextColored(load_color,"%%");
        }
        else {
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.load);
            ImGui::SameLine(0, 1.0f);
            HUDElements.TextColored(text_color,"%%");
            // ImGui::SameLine(150);
            // ImGui::Text("%s", "%");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp]){
            ImguiNextColumnOrNewRow();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", HUDElements.convert_to_fahrenheit(gpu_info.temp));
            else
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.temp);
            ImGui::SameLine(0, 1.0f);
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact])
                HUDElements.TextColored(HUDElements.colors.text, "°");
            else
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                    HUDElements.TextColored(HUDElements.colors.text, "°F");
                else
                    HUDElements.TextColored(HUDElements.colors.text, "°C");
        }

        if (gpu_info.junction_temp > -1 && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_junction_temp]) {
            ImguiNextColumnOrNewRow();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", HUDElements.convert_to_fahrenheit(gpu_info.junction_temp));
            else
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.junction_temp);
            ImGui::SameLine(0, 1.0f);
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                HUDElements.TextColored(HUDElements.colors.text, "°F");
            else
                HUDElements.TextColored(HUDElements.colors.text, "°C");
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "Jnc");
            ImGui::PopFont();
        }

        if (HUDElements.vendorID == 0x1002 || HUDElements.vendorID == 0x10de){
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_fan] && cpuStats.cpu_type != "APU"){
                ImguiNextColumnOrNewRow();
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.fan_speed);
                ImGui::SameLine(0, 1.0f);
                if (gpu_info.fan_rpm) {
                    ImGui::PushFont(HUDElements.sw_stats->font1);
                    HUDElements.TextColored(HUDElements.colors.text, "RPM");
                } else {
                    HUDElements.TextColored(HUDElements.colors.text, "%%");
                    ImGui::PushFont(HUDElements.sw_stats->font1);
                    ImGui::SameLine(0, 1.0f);
                    HUDElements.TextColored(HUDElements.colors.text, "FAN");
                }
                ImGui::PopFont();
            }
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.CoreClock);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MHz");
            ImGui::PopFont();
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_power]) {
            ImguiNextColumnOrNewRow();
            char str[16];
            snprintf(str, sizeof(str), "%.1f", gpu_info.powerUsage);
            if (strlen(str) > 4)
                right_aligned_text(text_color, HUDElements.ralign_width, "%.0f", gpu_info.powerUsage);
            else
                right_aligned_text(text_color, HUDElements.ralign_width, "%.1f", gpu_info.powerUsage);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "W");
            ImGui::PopFont();
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_voltage]) {
            ImguiNextColumnOrNewRow();
            right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.voltage);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "mV");
            ImGui::PopFont();
        }
    }
}

void HudElements::cpu_stats(){
    if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats]){
        ImguiNextColumnFirstItem();
        const char* cpu_text;
        if (HUDElements.params->cpu_text.empty())
            cpu_text = "CPU";
        else
            cpu_text = HUDElements.params->cpu_text.c_str();

        HUDElements.TextColored(HUDElements.colors.cpu, "%s", cpu_text);
        ImguiNextColumnOrNewRow();
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
            HUDElements.TextColored(load_color, "%%");
        }
        else {
            right_aligned_text(text_color, HUDElements.ralign_width, "%d", int(cpuStats.GetCPUDataTotal().percent));
            ImGui::SameLine(0, 1.0f);
            HUDElements.TextColored(HUDElements.colors.text, "%%");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_temp]){
            ImguiNextColumnOrNewRow();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", HUDElements.convert_to_fahrenheit(cpuStats.GetCPUDataTotal().temp));
            else
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuStats.GetCPUDataTotal().temp);
            ImGui::SameLine(0, 1.0f);
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact])
                HUDElements.TextColored(HUDElements.colors.text, "°");
            else
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                    HUDElements.TextColored(HUDElements.colors.text, "°F");
                else
                    HUDElements.TextColored(HUDElements.colors.text, "°C");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_mhz]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuStats.GetCPUDataTotal().cpu_mhz);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MHz");
            ImGui::PopFont();
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_power]){
            ImguiNextColumnOrNewRow();
            char str[16];
            snprintf(str, sizeof(str), "%.1f", cpuStats.GetCPUDataTotal().power);
            if (strlen(str) > 4)
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", cpuStats.GetCPUDataTotal().power);
            else
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", cpuStats.GetCPUDataTotal().power);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "W");
            ImGui::PopFont();
        }
    }
}


static float get_core_load_stat(void*,int);
static float get_core_load_stat(void *data, int idx){
    return ((CPUStats *)data)->GetCPUData().at(idx).percent;
}

void HudElements::core_load(){
    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_core_load])
        return;

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_core_bars]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            HUDElements.TextColored(HUDElements.colors.cpu, "CPU Cores");
            ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            ImguiNextColumnFirstItem();
        }
        char hash[40];
        snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_core_bars]);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        float width, height = 0;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal]){
            width = 150;
            height = HUDElements.params->font_size;
        } else {
            width = ImGui::GetWindowContentRegionWidth();
            height = 50;
        }

        if (ImGui::BeginChild("core_bars_window", ImVec2(width, height))) {
            ImGui::PlotHistogram(hash, get_core_load_stat, &cpuStats,
                                cpuStats.GetCPUData().size(), 0,
                                NULL, 0.0, 100.0,
                                ImVec2(width, height));
        }
        ImGui::EndChild();
        ImGui::PopFont();
        ImGui::PopStyleColor();
    } else {
        for (const CPUData &cpuData : cpuStats.GetCPUData())
        {
            ImguiNextColumnFirstItem();
            HUDElements.TextColored(HUDElements.colors.cpu, "CPU");
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.cpu,"%i", cpuData.cpu_id);
            ImGui::PopFont();
            ImguiNextColumnOrNewRow();
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
                HUDElements.TextColored(load_color, "%%");
                ImguiNextColumnOrNewRow();
            }
            else {
                right_aligned_text(text_color, HUDElements.ralign_width, "%i", int(cpuData.percent));
                ImGui::SameLine(0, 1.0f);
                HUDElements.TextColored(HUDElements.colors.text, "%%");
                ImguiNextColumnOrNewRow();
            }
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", cpuData.mhz);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MHz");
            ImGui::PopFont();
        }
    }
}

void HudElements::io_stats(){
#ifndef _WIN32
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] || HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
        ImguiNextColumnFirstItem();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
            HUDElements.TextColored(HUDElements.colors.io, "IO RD");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
            HUDElements.TextColored(HUDElements.colors.io, "IO RW");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read])
            HUDElements.TextColored(HUDElements.colors.io, "IO WR");

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read]){
            ImguiNextColumnOrNewRow();
            const float val = g_io_stats.per_second.read;
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MiB/s");
            ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
            ImguiNextColumnOrNewRow();
            const float val = g_io_stats.per_second.write;
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MiB/s");
            ImGui::PopFont();
        }
    }
#endif
}

void HudElements::vram(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vram]){
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.vram, "VRAM");
        ImguiNextColumnOrNewRow();
        // Add gtt_used to vram usage for APUs
        if (cpuStats.cpu_type == "APU")
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", gpu_info.memoryUsed + gpu_info.gtt_used);
        else
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", gpu_info.memoryUsed);
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "GiB");
            ImGui::PopFont();
        }

        if (gpu_info.memory_temp > -1 && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_temp]) {
            ImguiNextColumnOrNewRow();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", HUDElements.convert_to_fahrenheit(gpu_info.memory_temp));
            else
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", gpu_info.memory_temp);
            ImGui::SameLine(0, 1.0f);
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_temp_fahrenheit])
                HUDElements.TextColored(HUDElements.colors.text, "°F");
            else
                HUDElements.TextColored(HUDElements.colors.text, "°C");
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_clock]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", gpu_info.MemClock);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "MHz");
            ImGui::PopFont();
        }
    }
}

void HudElements::ram(){
#ifdef __linux__
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram]){
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.ram, "RAM");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", memused);
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
            ImGui::SameLine(0,1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "GiB");
            ImGui::PopFont();
        }
    }

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_swap]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", swapused);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "GiB");
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

    ImguiNextColumnFirstItem();
    HUDElements.TextColored(HUDElements.colors.ram, "PMEM");
    ImguiNextColumnOrNewRow();
    right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.resident, unit));
    ImGui::SameLine(0,1.0f);
    ImGui::PushFont(HUDElements.sw_stats->font1);
    HUDElements.TextColored(HUDElements.colors.text, "%s", unit);
    ImGui::PopFont();

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem_shared]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.shared, unit));
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "%s", unit);
        ImGui::PopFont();
    }

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_procmem_virt]){
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", format_units(proc_mem.virt, unit));
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
       HUDElements.TextColored(HUDElements.colors.text, "%s", unit);
        ImGui::PopFont();
    }
#endif
}

void HudElements::fps(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_only]){
        ImguiNextColumnFirstItem();
        if (HUDElements.params->fps_text.empty()){
            if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] || HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal])
                if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_short_names])
                    HUDElements.TextColored(HUDElements.colors.engine, "%s", engines_short[HUDElements.sw_stats->engine]);
                else
                    HUDElements.TextColored(HUDElements.colors.engine, "%s", "FPS");
            else
                if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_short_names])
                    HUDElements.TextColored(HUDElements.colors.engine, "%s", engines_short[HUDElements.sw_stats->engine]);
                else
                    HUDElements.TextColored(HUDElements.colors.engine, "%s", engines[HUDElements.sw_stats->engine]);
        } else {
            HUDElements.TextColored(HUDElements.colors.engine, "%s", HUDElements.params->fps_text.c_str());
        }

        ImguiNextColumnOrNewRow();
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
        if(!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal]){
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "FPS");
            ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frametime]){
            ImguiNextColumnOrNewRow();
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", 1000 / HUDElements.sw_stats->fps);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.text, "ms");
            ImGui::PopFont();
        }
    } else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImguiNextColumnOrNewRow();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", HUDElements.sw_stats->engineName.c_str());
    }
}

void HudElements::fps_only(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps_only]){
        ImguiNextColumnFirstItem();
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
        HUDElements.TextColored(load_color, "%.0f", HUDElements.sw_stats->fps);
    }
}

void HudElements::gpu_name(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_name] && !HUDElements.sw_stats->gpuName.empty()){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine,
            "%s", HUDElements.sw_stats->gpuName.c_str());
        ImGui::PopFont();
    }
}

void HudElements::engine_version(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        if (HUDElements.is_vulkan) {
            if ((HUDElements.sw_stats->engine == EngineTypes::DXVK || HUDElements.sw_stats->engine == EngineTypes::VKD3D)){
                HUDElements.TextColored(HUDElements.colors.engine,
                    "%s/%d.%d.%d", HUDElements.sw_stats->engineVersion.c_str(),
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            } else {
                HUDElements.TextColored(HUDElements.colors.engine,
                    "%d.%d.%d",
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            }
        } else {
            HUDElements.TextColored(HUDElements.colors.engine,
                "%d.%d%s", HUDElements.sw_stats->version_gl.major, HUDElements.sw_stats->version_gl.minor,
                HUDElements.sw_stats->version_gl.is_gles ? " ES" : "");
        }
        ImGui::PopFont();
    }
}

void HudElements::vulkan_driver(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vulkan_driver] && !HUDElements.sw_stats->driverName.empty()){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine,
            "%s", HUDElements.sw_stats->driverName.c_str());
        ImGui::PopFont();
    }
}

void HudElements::arch(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_arch]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "" MANGOHUD_ARCH);
        ImGui::PopFont();
    }
}

void HudElements::wine(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_wine]){
        ImguiNextColumnFirstItem();
        if (!wineVersion.empty()){
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.wine, "%s", wineVersion.c_str());
            ImGui::PopFont();
        }
    }
}

static inline double TransformForward_Custom(double v, void*) {
    if (v > 50)
        v = 49.9;

    return v;
}

static inline double TransformInverse_Custom(double v, void*) {
   return v;
}

void HudElements::frame_timing(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_timing]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            HUDElements.TextColored(HUDElements.colors.engine, "%s", "Frametime");
            ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            right_aligned_text(HUDElements.colors.text, ImGui::GetContentRegionAvail().x, "min: %.1fms, max: %.1fms", min_frametime, max_frametime);
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y / 2));
            ImguiNextColumnFirstItem();
        }
        char hash[40];
        snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_frame_timing]);
        HUDElements.sw_stats->stat_selector = OVERLAY_PLOTS_frame_timing;
        HUDElements.sw_stats->time_dividor = 1000000.0f; /* ns -> ms */
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        double min_time = 0.0f;
        double max_time = 50.0f;
        float width, height = 0;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal]){
            width = 150;
            height = HUDElements.params->font_size * 0.85;
        } else {
            width = ImGui::GetWindowContentRegionWidth();
            height = max_time;
        }

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_dynamic_frame_timing]){
            min_time = min_frametime;
            max_time = max_frametime;
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_timing_detailed]){
            height = 125;
        }

        if (ImGui::BeginChild("my_child_window", ImVec2(width, height), false, ImGuiWindowFlags_NoDecoration)) {
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_histogram]){
                ImGui::PlotHistogram(hash, get_time_stat, HUDElements.sw_stats,
                                    ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                                    NULL, min_time, max_time,
                                    ImVec2(width, height));
            } else {
#ifndef __linux__
                ImGui::PlotLines(hash, get_time_stat, HUDElements.sw_stats,
                                ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                                NULL, min_time, max_time,
                                ImVec2(width, height));
#else

                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal]) {
                    ImGui::PlotLines(hash, get_time_stat, HUDElements.sw_stats,
                    ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                    NULL, min_time, max_time,
                    ImVec2(width, height));
                } else {
                    if (ImPlot::BeginPlot("My Plot", ImVec2(width, height), ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
                        ImPlotStyle& style = ImPlot::GetStyle();
                        style.Colors[ImPlotCol_PlotBg] = ImVec4(0.92f, 0.92f, 0.95f, 0.00f);
                        style.Colors[ImPlotCol_AxisGrid] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
                        style.Colors[ImPlotCol_AxisTick] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
                        ImPlotAxisFlags ax_flags_x = ImPlotAxisFlags_NoDecorations;
                        ImPlotAxisFlags ax_flags_y = ImPlotAxisFlags_NoDecorations;
                        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_timing_detailed])
                            ax_flags_y = ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoMenus;

                        ImPlot::SetupAxes(nullptr, nullptr, ax_flags_x, ax_flags_y);
                        ImPlot::SetupAxisScale(ImAxis_Y1, TransformForward_Custom, TransformInverse_Custom);
                        ImPlot::SetupAxesLimits(0, 200, min_time, max_time);
                        ImPlot::SetNextLineStyle(HUDElements.colors.frametime, 1.5);
                        ImPlot::PlotLine("frametime line", frametime_data.data(), frametime_data.size());
                        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_throttling_status_graph] && throttling){
                            ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 1.5);
                            ImPlot::PlotLine("power line", throttling->power.data(), throttling->power.size());
                            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 1.5);
                            ImPlot::PlotLine("thermal line", throttling->thermal.data(), throttling->thermal.size());
                        }
                        ImPlot::EndPlot();
                    }
                }
#endif
            }
        }
        ImGui::EndChild();
#ifdef __linux__
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_throttling_status_graph] && throttling){
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y / 2));

            if (throttling->power_throttling()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", ICON_FK_SQUARE);
                ImGui::SameLine();
                ImGui::Text("Power throttling");
            }

            ImGui::Dummy(ImVec2(0.0f, real_font_size.y / 2));

            if (throttling->thermal_throttling()) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", ICON_FK_SQUARE);
                ImGui::SameLine();
                ImGui::Text("Thermal throttling");
            }
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
#endif
    }
}

void HudElements::media_player(){
#ifdef HAVE_DBUS
    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_media_player])
        return;

    ImguiNextColumnFirstItem();
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
        ImguiNextColumnFirstItem();
        const auto res  = ImGui::GetIO().DisplaySize;
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine, "Resolution");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "%.0fx%.0f", res.x, res.y);
        ImGui::PopFont();
    }
}

void HudElements::show_fps_limit(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_show_fps_limit]){
        int fps = 0;
        if (fps_limit_stats.targetFrameTime.count())
            fps = 1000000000 / fps_limit_stats.targetFrameTime.count();
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        const char* method = fps_limit_stats.method == FPS_LIMIT_METHOD_EARLY ? "early" : "late";
        HUDElements.TextColored(HUDElements.colors.engine, "%s (%s)","FPS limit",method);
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", fps);
        ImGui::PopFont();
    }
}

void HudElements::custom_text_center(){
    if (HUDElements.place >= 0 &&
        static_cast<size_t>(HUDElements.place) < HUDElements.ordered_functions.size()) {

        if (!HUDElements.sw_stats || !HUDElements.sw_stats->font1) {
            return;
        }

        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);

        const std::string& value = HUDElements.ordered_functions[HUDElements.place].value;

        center_text(value);
        HUDElements.TextColored(HUDElements.colors.text, "%s", value.c_str());

        ImGui::NewLine();
        ImGui::PopFont();
    }
}

void HudElements::custom_text(){
    ImguiNextColumnFirstItem();
    ImGui::PushFont(HUDElements.sw_stats->font1);
    const char* value;
    if (size_t(HUDElements.place) < HUDElements.ordered_functions.size())
        value = HUDElements.ordered_functions[HUDElements.place].value.c_str();
    else {
        ImGui::PopFont();
        return;
    }
    HUDElements.TextColored(HUDElements.colors.text, "%s",value);
    ImGui::PopFont();
}

void HudElements::_exec(){
    //const std::string& value = HUDElements.ordered_functions[HUDElements.place].second;
    ImGui::PushFont(HUDElements.sw_stats->font1);
    ImguiNextColumnFirstItem();
    for (auto& item : HUDElements.exec_list){
        if (item.pos == HUDElements.place){
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", item.ret.c_str());
        }
    }
    ImGui::PopFont();
}

void HudElements::gamemode(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gamemode]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "GAMEMODE");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.gamemode_bol ? "ON" : "OFF");
        ImGui::PopFont();
    }
}

void HudElements::vkbasalt(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vkbasalt]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "VKBASALT");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.vkbasalt_bol ? "ON" : "OFF");
        ImGui::PopFont();
    }
}

void HudElements::battery(){
#ifdef __linux__
    if (Battery_Stats.batt_count > 0) {
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery]) {
            ImguiNextColumnFirstItem();
            if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact])
                HUDElements.TextColored(HUDElements.colors.battery, "BAT");
            else
                HUDElements.TextColored(HUDElements.colors.battery, "BATT");
            ImguiNextColumnOrNewRow();
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
                HUDElements.TextColored(HUDElements.colors.text, "%%");
            }
            if (Battery_Stats.current_watt != 0) {
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_watt]){
                    ImguiNextColumnOrNewRow();
                    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact] && Battery_Stats.current_watt >= 10.0f)
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", Battery_Stats.current_watt);
                    else
                        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.1f", Battery_Stats.current_watt);
                    ImGui::SameLine(0,1.0f);
                    ImGui::PushFont(HUDElements.sw_stats->font1);
                    HUDElements.TextColored(HUDElements.colors.text, "W");
                    ImGui::PopFont();
                }
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_battery_time]) {
                    float hours;
                    float minutes;
                    minutes = std::modf(Battery_Stats.remaining_time, &hours);
                    minutes *= 60;
                    char time_buffer[32];
                    snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d", static_cast<int>(hours), static_cast<int>(minutes));

                    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact]){
                        ImGui::TableNextRow();
                        ImGui::NextColumn();
                        ImGui::PushFont(HUDElements.sw_stats->font1);
                        ImGuiTableSetColumnIndex(0);
                        HUDElements.TextColored(HUDElements.colors.text, "%s", "Remaining Time");
                        ImGui::PopFont();
                        ImGuiTableSetColumnIndex(2);
                    } else {
                        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal])
                            ImguiNextColumnOrNewRow();

                        ImguiNextColumnOrNewRow();
                    }
                    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hud_compact])
                        ImGuiTableSetColumnIndex(0);

                    right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", time_buffer);
                }
            } else {
                ImguiNextColumnOrNewRow();
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_PLUG);
            }
        }

    }
#endif
}

void HudElements::gamescope_fsr(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fsr] && HUDElements.g_fsrUpscale >= 0) {
        ImguiNextColumnFirstItem();
        string FSR_TEXT;
        ImVec4 FSR_COLOR;
        if (HUDElements.g_fsrUpscale){
            FSR_TEXT = "ON";
            FSR_COLOR = HUDElements.colors.fps_value_high;
        } else {
            FSR_TEXT = "OFF";
            FSR_COLOR = HUDElements.colors.fps_value_low;
        }

        HUDElements.TextColored(HUDElements.colors.engine, "%s", "FSR");
        ImguiNextColumnOrNewRow();
        right_aligned_text(FSR_COLOR, HUDElements.ralign_width, "%s", FSR_TEXT.c_str());
        if (HUDElements.g_fsrUpscale){
            if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_hide_fsr_sharpness]) {
                ImguiNextColumnOrNewRow();
                right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", HUDElements.g_fsrSharpness);
                ImGui::SameLine(0,1.0f);
                ImGui::PushFont(HUDElements.sw_stats->font1);
                HUDElements.TextColored(HUDElements.colors.text, "Sharp");
                ImGui::PopFont();
            }
        }
    }
}

void HudElements::gamescope_frame_timing(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_debug]) {
        static std::vector<float>::iterator min, max;
        static double min_time = 0.0f;
        static double max_time = 50.0f;
        if (HUDElements.gamescope_debug_app.size() > 0 && HUDElements.gamescope_debug_app.back() > -1){
            auto min = std::min_element(HUDElements.gamescope_debug_app.begin(),
                                        HUDElements.gamescope_debug_app.end());
            auto max = std::max_element(HUDElements.gamescope_debug_app.begin(),
                                        HUDElements.gamescope_debug_app.end());
            
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            HUDElements.TextColored(HUDElements.colors.engine, "%s", "App");
            ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "min: %.1fms, max: %.1fms", min[0], max[0]);
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y / 2));
            ImguiNextColumnFirstItem();
            ImGui::PopFont();
            char hash[40];
            snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_frame_timing]);
            HUDElements.sw_stats->stat_selector = OVERLAY_PLOTS_frame_timing;
            HUDElements.sw_stats->time_dividor = 1000000.0f; /* ns -> ms */

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            if (ImGui::BeginChild("gamescope_app_window", ImVec2(ImGui::GetWindowContentRegionWidth(), 50))) {
                ImGui::PlotLines("", HUDElements.gamescope_debug_app.data(),
                        HUDElements.gamescope_debug_app.size(), 0,
                        NULL, min_time, max_time,
                            ImVec2(ImGui::GetWindowContentRegionWidth(), 50));
            }
            ImGui::PopStyleColor();
            ImGui::EndChild();
        }
        if (HUDElements.gamescope_debug_latency.size() > 0 && HUDElements.gamescope_debug_latency.back() > -1){
            ImguiNextColumnOrNewRow();
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            ImGui::PushFont(HUDElements.sw_stats->font1);
            HUDElements.TextColored(HUDElements.colors.engine, "%s", "Latency");
            ImGui::TableNextRow();
            ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
            min = std::min_element(HUDElements.gamescope_debug_latency.begin(),
                                   HUDElements.gamescope_debug_latency.end());
            max = std::max_element(HUDElements.gamescope_debug_latency.begin(),
                                   HUDElements.gamescope_debug_latency.end());
            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width * 1.3, "min: %.1fms, max: %.1fms", min[0], max[0]);
            ImGui::PopFont();
            ImguiNextColumnFirstItem();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0,0,1,1));
            if (ImGui::BeginChild("gamescope_latency_window", ImVec2(ImGui::GetWindowContentRegionWidth(), 50))) {
                ImGui::PlotLines("", HUDElements.gamescope_debug_latency.data(),
                        HUDElements.gamescope_debug_latency.size(), 0,
                        NULL, min_time, max_time,
                        ImVec2(ImGui::GetWindowContentRegionWidth(), 50));
            }
            ImGui::PopStyleColor(2);
            ImGui::EndChild();
        }
    }
}

void HudElements::device_battery()
{
#ifdef __linux__
    std::unique_lock<std::mutex> l(device_lock);
    if (!HUDElements.params->device_battery.empty()) {
        if (device_found) {
            for (int i = 0; i < device_count; i++) {
                std::string battery = device_data[i].battery;
                std::string name = device_data[i].name;
                std::string battery_percent = device_data[i].battery_percent;
                bool report_percent = device_data[i].report_percent;
                bool charging = device_data[i].is_charging;

                ImguiNextColumnFirstItem();
                ImGui::PushFont(HUDElements.sw_stats->font1);
                HUDElements.TextColored(HUDElements.colors.engine, "%s", name.c_str());
                ImguiNextColumnOrNewRow();
                if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_device_battery_icon]) {
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
                        HUDElements.TextColored(HUDElements.colors.text, "%%");
                    }
                    else {
                        if (battery == "Unknown")
                            right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", ICON_FK_USB);
                        else
                            right_aligned_text(HUDElements.colors.text,HUDElements.ralign_width, "%s", battery.c_str());
                    }
                }
                if (device_count > 1 && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_horizontal])
                    ImGui::TableNextRow();
                ImGui::PopFont();
            }
        }
    }
#endif
}

void HudElements::frame_count(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_count]){
        ImguiNextColumnFirstItem();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.engine, "Frame Count");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%" PRIu64, HUDElements.sw_stats->n_frames);
        ImGui::PopFont();
    }
}

void HudElements::fan(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fan] && fan_speed != -1) {
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "FAN");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text,HUDElements.ralign_width, "%i", fan_speed);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "RPM");
        ImGui::PopFont();
    }
}

void HudElements::throttling_status(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_throttling_status]){
        if (gpu_info.is_power_throttled || gpu_info.is_current_throttled || gpu_info.is_temp_throttled || gpu_info.is_other_throttled){
            ImguiNextColumnFirstItem();
            HUDElements.TextColored(HUDElements.colors.engine, "%s", "Throttling");
            ImguiNextColumnOrNewRow();
            ImguiNextColumnOrNewRow();
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

void HudElements::duration(){
    ImGui::PushFont(HUDElements.sw_stats->font1);
    ImguiNextColumnFirstItem();
    HUDElements.TextColored(HUDElements.colors.engine, "%s", "Duration");
    ImguiNextColumnOrNewRow();
    std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsedTime = currentTime - HUDElements.overlay_start;
    int hours = std::chrono::duration_cast<std::chrono::hours>(elapsedTime).count();
    int minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsedTime).count() % 60;
    int seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count() % 60;
    if (hours > 0)
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%02d:%02d:%02d", hours, minutes, seconds);
    else if (minutes > 0)
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%02d:%02d", minutes, seconds);
    else
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%02d", seconds);
    ImGui::PopFont();
}

void HudElements::graphs(){
    ImguiNextColumnFirstItem();
    ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
    const std::string& value = HUDElements.ordered_functions[HUDElements.place].value;
    assert(kMaxGraphEntries >= graph_data.size());
    std::vector<float> arr(kMaxGraphEntries - graph_data.size());

    ImGui::PushFont(HUDElements.sw_stats->font1);
    if (value == "cpu_load"){
        for (auto& it : graph_data){
            arr.push_back(float(it.cpu_load));
        }
        HUDElements.max = 100;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "CPU Load");
    }

    if (value == "gpu_load"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_load));
        }
        HUDElements.max = 100;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "GPU Load");
    }

    if (value == "cpu_temp"){
        for (auto& it : graph_data){
            arr.push_back(float(it.cpu_temp));
        }
        if (int(arr.back()) > HUDElements.cpu_temp_max)
            HUDElements.cpu_temp_max = arr.back();

        HUDElements.max = HUDElements.cpu_temp_max;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "CPU Temp");
    }

    if (value == "gpu_temp"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_temp));
        }
        if (int(arr.back()) > HUDElements.gpu_temp_max)
            HUDElements.gpu_temp_max = arr.back();

        HUDElements.max = HUDElements.gpu_temp_max;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "GPU Temp");
    }

    if (value == "gpu_core_clock"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_core_clock));
        }
        if (int(arr.back()) > HUDElements.gpu_core_max)
            HUDElements.gpu_core_max = arr.back();

        HUDElements.max = HUDElements.gpu_core_max;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "GPU Core Clock");
    }

    if (value == "gpu_mem_clock"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_mem_clock));
        }
        if (int(arr.back()) > HUDElements.gpu_mem_max)
            HUDElements.gpu_mem_max = arr.back();

        HUDElements.max = HUDElements.gpu_mem_max;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "GPU Mem Clock");
    }

    if (value == "vram"){
        for (auto& it : graph_data){
            arr.push_back(float(it.gpu_vram_used));
        }

        HUDElements.max = gpu_info.memoryTotal;
        HUDElements.min = 0;
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "VRAM");
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
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "RAM");
    }
#endif
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f,5.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
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

void HudElements::exec_name(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_exec_name]){
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "Exe name");
        ImguiNextColumnOrNewRow();
        ImVec2 text_size = ImGui::CalcTextSize(global_proc_name.c_str());
        right_aligned_text(HUDElements.colors.text, text_size.x, global_proc_name.c_str());
        ImGui::PopFont();
    }
}

void HudElements::fps_metrics(){
    for (auto& metric : fpsmetrics->metrics){
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", metric.display_name.c_str());
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", metric.value);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "FPS");
        ImGui::PopFont();
        ImguiNextColumnOrNewRow();
    }

}

void HudElements::hdr() {
    if (HUDElements.hdr_status > 0) {
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "HDR");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.fps_value_high, HUDElements.ralign_width, "ON");
    }
}

void HudElements::refresh_rate() {
    if (HUDElements.refresh > 0) {
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "Display Hz");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%i", HUDElements.refresh);
        ImGui::PopFont();
    }
}

void HudElements::winesync() {
    if (!HUDElements.winesync_ptr)
        HUDElements.winesync_ptr = std::make_unique<WineSync>();

    if (HUDElements.winesync_ptr->valid()) {
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImguiNextColumnFirstItem();
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "WSYNC");
        ImguiNextColumnOrNewRow();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", HUDElements.winesync_ptr->get_method().c_str());
        ImGui::PopFont();
    }
}

void HudElements::present_mode() {
    ImguiNextColumnFirstItem();
    ImGui::PushFont(HUDElements.sw_stats->font1);
    if (HUDElements.is_vulkan)
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "Present Mode");
    else
        HUDElements.TextColored(HUDElements.colors.engine, "%s", "VSYNC");
    ImguiNextColumnOrNewRow();
    HUDElements.TextColored(HUDElements.colors.text, "%s\n", HUDElements.get_present_mode().c_str());
    ImGui::PopFont();
}

void HudElements::network() {
#ifdef __linux__
    if (HUDElements.net && HUDElements.net->should_reset)
        HUDElements.net.reset(new Net);

    if (!HUDElements.net)
        HUDElements.net = std::make_unique<Net>();

    for (auto& iface : HUDElements.net->interfaces){
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        HUDElements.TextColored(HUDElements.colors.network, "%.8s", iface.name.c_str());
        ImGui::TableNextColumn();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", iface.txBps / 1000.f);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "KB/s %s", ICON_FK_ARROW_UP);
        ImGui::PopFont();
        ImGui::TableNextColumn();
        right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%.0f", iface.rxBps / 1000.f);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        HUDElements.TextColored(HUDElements.colors.text, "KB/s %s", ICON_FK_ARROW_DOWN);
        ImGui::PopFont();
    }
#endif
}

void HudElements::_display_session() {
    ImGui::PushFont(HUDElements.sw_stats->font1);
    ImguiNextColumnFirstItem();
    HUDElements.TextColored(HUDElements.colors.engine, "%s", "Display server");
    ImguiNextColumnOrNewRow();
    static std::map<display_servers, std::string> servers {
        {WAYLAND, {"WAYLAND"}},
        {XWAYLAND, {"XWAYLAND"}},
        {XORG, {"XORG"}}
    };
    right_aligned_text(HUDElements.colors.text, HUDElements.ralign_width, "%s", servers[HUDElements.display_server].c_str());
    ImGui::PopFont();
}

void HudElements::sort_elements(const std::pair<std::string, std::string>& option) {
    const auto& param = option.first;
    const auto& value = option.second;

    // Initialize a map of display parameters and their corresponding functions.
    const std::map<std::string, Function> display_params = {
        {"version", {version}},
        {"time", {time}},
        {"gpu_stats", {gpu_stats}},
        {"cpu_stats", {cpu_stats}},
        {"core_load", {core_load}},
        {"io_read", {io_stats}},
        {"io_write", {io_stats}},
        {"arch", {arch}},
        {"wine", {wine}},
        {"procmem", {procmem}},
        {"gamemode", {gamemode}},
        {"vkbasalt", {vkbasalt}},
        {"engine_version", {engine_version}},
        {"vulkan_driver", {vulkan_driver}},
        {"resolution", {resolution}},
        {"show_fps_limit", {show_fps_limit}},
        {"vram", {vram}},
        {"ram", {ram}},
        {"fps", {fps}},
        {"gpu_name", {gpu_name}},
        {"frame_timing", {frame_timing}},
        {"media_player", {media_player}},
        {"custom_text", {custom_text}},
        {"custom_text_center", {custom_text_center}},
        {"exec", {_exec}},
        {"battery", {battery}},
        {"fps_only", {fps_only}},
        {"fsr", {gamescope_fsr}},
        {"debug", {gamescope_frame_timing}},
        {"device_battery", {device_battery}},
        {"frame_count", {frame_count}},
        {"fan", {fan}},
        {"throttling_status", {throttling_status}},
        {"exec_name", {exec_name}},
        {"duration", {duration}},
        {"graphs", {graphs}},
        {"fps_metrics", {fps_metrics}},
        {"hdr", {hdr}},
        {"refresh_rate", {refresh_rate}},
        {"winesync", {winesync}},
        {"present_mode", {present_mode}},
        {"network", {network}},
        {"display_session", {_display_session}}

    };

    auto check_param = display_params.find(param);
    if (check_param != display_params.end()) {
        const Function& func = check_param->second;

        if (param == "debug") {
            ordered_functions.push_back({gamescope_frame_timing, "gamescope_frame_timing", value});
        } else if (param == "fsr") {
            ordered_functions.push_back({gamescope_fsr, "gamescope_fsr", value});
        } else if (param == "io_read" || param == "io_write") {
            // Don't add twice
            if (std::none_of(ordered_functions.begin(), ordered_functions.end(),
                [](const auto& a) { return a.name == "io_stats"; })) {
                ordered_functions.push_back({io_stats, "io_stats", value});
            }
        } else if (param == "exec") {
            ordered_functions.push_back({_exec, "exec", value});
            exec_list.push_back({int(ordered_functions.size() - 1), value});
        } else if (param == "graphs") {
            // Handle graphs parameter
            if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_graphs]) {
                HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_graphs] = true;
            }

            auto values = str_tokenize(value);
            for (auto& val : values) {
                if (find(permitted_params.begin(), permitted_params.end(), val) != permitted_params.end()) {
                    ordered_functions.push_back({graphs, "graph: " + val, val});
                } else {
                    SPDLOG_ERROR("Unrecognized graph type: {}", val);
                }
            }
        } else {
            // Use this to always add to the front of the vector
            // ordered_functions.insert(ordered_functions.begin(), std::make_pair(param, value));
            ordered_functions.push_back({func.run, param, value});
        }
    }
    return;
}

void HudElements::legacy_elements(){
    string value = "NULL";
    ordered_functions.clear();
    if (params->enabled[OVERLAY_PARAM_ENABLED_time])
        ordered_functions.push_back({time, "time", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_version])
        ordered_functions.push_back({version, "version", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats])
        ordered_functions.push_back({gpu_stats, "gpu_stats", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats])
        ordered_functions.push_back({cpu_stats, "cpu_stats", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_core_load])
        ordered_functions.push_back({core_load, "core_load", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_io_read] || params->enabled[OVERLAY_PARAM_ENABLED_io_write])
        ordered_functions.push_back({io_stats, "io_stats", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_vram])
        ordered_functions.push_back({vram, "vram", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_ram])
        ordered_functions.push_back({ram, "ram", value});
    if (!params->network.empty())
        ordered_functions.push_back({network, "network", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_battery])
        ordered_functions.push_back({battery, "battery", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_fan])
        ordered_functions.push_back({fan, "fan", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_fsr])
        ordered_functions.push_back({gamescope_fsr, "gamescope_fsr", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_hdr])
        ordered_functions.push_back({hdr, "hdr", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_throttling_status])
        ordered_functions.push_back({throttling_status, "throttling_status", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_fps])
        ordered_functions.push_back({fps, "fps", value});
    for (const auto& pair : options) {
        if (pair.first.find("graphs") != std::string::npos) {
            std::stringstream ss(pair.second);
            std::string token;
            while (std::getline(ss, token, ',')){
                ordered_functions.push_back({graphs, "graphs", token});
            }
        }
    }
    if (!params->fps_metrics.empty())
        ordered_functions.push_back({fps_metrics, "fps_metrics", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_fps_only])
        ordered_functions.push_back({fps_only, "fps_only", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_engine_version])
        ordered_functions.push_back({engine_version, "engine_version", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_name])
        ordered_functions.push_back({gpu_name, "gpu_name", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_vulkan_driver])
        ordered_functions.push_back({vulkan_driver, "vulkan_driver", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_arch])
        ordered_functions.push_back({arch, "arch", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_wine])
        ordered_functions.push_back({wine, "wine", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_frame_timing])
        ordered_functions.push_back({frame_timing, "frame_timing", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_frame_count])
        ordered_functions.push_back({frame_count, "frame_count", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_debug] && !params->enabled[OVERLAY_PARAM_ENABLED_horizontal])
        ordered_functions.push_back({gamescope_frame_timing, "gamescope_frame_timing", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_gamemode])
        ordered_functions.push_back({gamemode, "gamemode", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_vkbasalt])
        ordered_functions.push_back({vkbasalt, "vkbasalt", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_show_fps_limit])
        ordered_functions.push_back({show_fps_limit, "show_fps_limit", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_resolution])
        ordered_functions.push_back({resolution, "resolution", value});
    if (!params->device_battery.empty() )
        ordered_functions.push_back({device_battery, "device_battery", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_media_player])
        ordered_functions.push_back({media_player, "media_player", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_exec_name])
        ordered_functions.push_back({exec_name, "exec_name", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_duration])
        ordered_functions.push_back({duration, "duration", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_winesync])
        ordered_functions.push_back({winesync, "winesync", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_present_mode])
        ordered_functions.push_back({present_mode, "present_mode", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_refresh_rate])
        ordered_functions.push_back({refresh_rate, "refresh_rate", value});
    if (params->enabled[OVERLAY_PARAM_ENABLED_display_server])
        ordered_functions.push_back({_display_session, "display_session", value});
}

void HudElements::update_exec(){
#ifdef __linux__
    if (!HUDElements.shell)
        HUDElements.shell = std::make_unique<Shell>();

    for(auto& item : exec_list){
        std::string ret = HUDElements.shell->exec(item.value + "\n");
        // use the previous ret if we get bad system call
        if (ret.find("Bad system call") == std::string::npos)
            item.ret = ret;
    }
#endif
}

HudElements HUDElements;
