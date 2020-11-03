#include <algorithm>
#include "hud_elements.h"
#include "cpu.h"
#include "memory.h"
#include "mesa/util/macros.h"
// void HudElements::newRow(){
//     ImGui::TableNextRow();
// }

// void HudElements::text(){
//     auto text = HUDElements.options.at(HUDElements.place);
//     text.erase(std::remove(text.begin(), text.end(), '%'), text.end());
//     size_t underscores_found = text.find("_");
//     // remove all underscores
//     for (size_t i = 0; i < underscores_found; i++)
//     {
//         text.replace(text.find("_"), 1, " ");
//     }

//     printf("%s\n", text.c_str());
//     ImGui::Text(text.c_str());
// }

void HudElements::time(){
    ImGui::TableNextRow();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.00f), "%s", HUDElements.sw_stats->time.c_str());
}

void HudElements::version(){
    ImGui::TableNextRow();
    ImGui::Text("%s", MANGOHUD_VERSION);
}

void HudElements::gpu_stats(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats]){
        ImGui::TableNextRow();
        const char* gpu_text;
        if (HUDElements.params->gpu_text.empty())
        gpu_text = "GPU";
        else
        gpu_text = HUDElements.params->gpu_text.c_str();
        ImGui::TextColored(HUDElements.sw_stats->colors.gpu, "%s", gpu_text);
        ImGui::TableNextCell();
        auto text_color = HUDElements.sw_stats->colors.text;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_load_change]){
        struct LOAD_DATA gpu_data = {HUDElements.sw_stats->colors.gpu_load_high,
                                        HUDElements.sw_stats->colors.gpu_load_med,
                                        HUDElements.sw_stats->colors.gpu_load_low,
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
        ImGui::TableNextCell();
        right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.temp);
        ImGui::SameLine(0, 1.0f);
        ImGui::Text("°C");
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock] || HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_power])
        ImGui::TableNextRow();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock]){
        ImGui::TableNextCell();
        right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.CoreClock);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("MHz");
        ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_power]) {
        ImGui::TableNextCell();
        right_aligned_text(text_color, HUDElements.ralign_width, "%i", gpu_info.powerUsage);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("W");
        ImGui::PopFont();
        }
    }
}

void HudElements::cpu_stats(){
    if(HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_stats]){
        ImGui::TableNextRow();
        const char* cpu_text;
        if (HUDElements.params->cpu_text.empty())
        cpu_text = "CPU";
        else
        cpu_text = HUDElements.params->cpu_text.c_str();
        ImGui::TextColored(HUDElements.sw_stats->colors.cpu, "%s", cpu_text);
        ImGui::TableNextCell();
        auto text_color = HUDElements.sw_stats->colors.text;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_cpu_load_change]){
        int cpu_load_percent = int(cpuStats.GetCPUDataTotal().percent);
        struct LOAD_DATA cpu_data = {HUDElements.sw_stats->colors.cpu_load_high,
                                        HUDElements.sw_stats->colors.cpu_load_med,
                                        HUDElements.sw_stats->colors.cpu_load_low,
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
        ImGui::TableNextCell();
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%i", cpuStats.GetCPUDataTotal().temp);
        ImGui::SameLine(0, 1.0f);
        ImGui::Text("°C");
        }
    }
}

void HudElements::core_load(){
         int i = 0;
         for (const CPUData &cpuData : cpuStats.GetCPUData())
         {
            ImGui::TableNextRow();
            ImGui::TextColored(HUDElements.sw_stats->colors.cpu, "CPU");
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::TextColored(HUDElements.sw_stats->colors.cpu,"%i", i);
            ImGui::PopFont();
            ImGui::TableNextCell();
            right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%i", int(cpuData.percent));
            ImGui::SameLine(0, 1.0f);
            ImGui::Text("%%");
            ImGui::TableNextCell();
            right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%i", cpuData.mhz);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
            i++;
         }
}
void HudElements::io_stats(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] || HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
        auto sampling = HUDElements.params->fps_sampling_period;
        ImGui::TableNextRow();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
        ImGui::TextColored(HUDElements.sw_stats->colors.io, "IO RD");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write])
        ImGui::TextColored(HUDElements.sw_stats->colors.io, "IO RW");
        else if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write] && !HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read])
        ImGui::TextColored(HUDElements.sw_stats->colors.io, "IO WR");

        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_read]){
        ImGui::TableNextCell();
        float val = HUDElements.sw_stats->io.diff.read * 1000000 / sampling;
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("MiB/s");
        ImGui::PopFont();
        }
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_io_write]){
        ImGui::TableNextCell();
        float val = HUDElements.sw_stats->io.diff.write * 1000000 / sampling;
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, val < 100 ? "%.1f" : "%.f", val);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("MiB/s");
        ImGui::PopFont();
        }
    }
}

void HudElements::vram(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vram]){
        ImGui::TableNextRow();
        ImGui::TextColored(HUDElements.sw_stats->colors.vram, "VRAM");
        ImGui::TableNextCell();
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%.1f", gpu_info.memoryUsed);
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("GiB");
        ImGui::PopFont();
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_clock]){
            ImGui::TableNextCell();
            right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%i", gpu_info.MemClock);
            ImGui::SameLine(0, 1.0f);
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::Text("MHz");
            ImGui::PopFont();
        }
    }
}
void HudElements::ram(){
#ifdef __gnu_linux__  
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_ram]){
         ImGui::TableNextRow();
         ImGui::TextColored(HUDElements.sw_stats->colors.ram, "RAM");
         ImGui::TableNextCell();
         right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%.1f", memused);
         ImGui::SameLine(0,1.0f);
         ImGui::PushFont(HUDElements.sw_stats->font1);
         ImGui::Text("GiB");
         ImGui::PopFont();
      }
#endif
}

void HudElements::fps(){
if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps]){
        ImGui::TableNextRow();
        if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_fps] && HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImGui::TextColored(HUDElements.sw_stats->colors.engine, "%s", HUDElements.is_vulkan ? HUDElements.sw_stats->engineName.c_str() : "OpenGL");
        }
        ImGui::TextColored(HUDElements.sw_stats->colors.engine, "%s", HUDElements.is_vulkan ? HUDElements.sw_stats->engineName.c_str() : "OpenGL");
        ImGui::TableNextCell();
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%.0f", HUDElements.sw_stats->fps);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("FPS");
        ImGui::PopFont();
        ImGui::TableNextCell();
        right_aligned_text(HUDElements.sw_stats->colors.text, HUDElements.ralign_width, "%.1f", 1000 / HUDElements.sw_stats->fps);
        ImGui::SameLine(0, 1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("ms");
        ImGui::PopFont();
    }
}

void HudElements::gpu_name(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_gpu_name] && !HUDElements.sw_stats->gpuName.empty()){
        ImGui::TableNextRow();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.sw_stats->colors.engine,
            "%s", HUDElements.sw_stats->gpuName.c_str());
        ImGui::PopFont();
    }
}
void HudElements::engine_version(){
    ImGui::TableNextRow();
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_engine_version]){
        ImGui::PushFont(HUDElements.sw_stats->font1);
        if (HUDElements.is_vulkan) {
            if ((HUDElements.sw_stats->engineName == "DXVK" || HUDElements.sw_stats->engineName == "VKD3D")){
                ImGui::TextColored(HUDElements.sw_stats->colors.engine,
                    "%s/%d.%d.%d", HUDElements.sw_stats->engineVersion.c_str(),
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            } else {
                ImGui::TextColored(HUDElements.sw_stats->colors.engine,
                    "%d.%d.%d",
                    HUDElements.sw_stats->version_vk.major,
                    HUDElements.sw_stats->version_vk.minor,
                    HUDElements.sw_stats->version_vk.patch);
            }
        } else {
        ImGui::TextColored(HUDElements.sw_stats->colors.engine,
            "%d.%d%s", HUDElements.sw_stats->version_gl.major, HUDElements.sw_stats->version_gl.minor,
            HUDElements.sw_stats->version_gl.is_gles ? " ES" : "");
        }
        // ImGui::SameLine();
        ImGui::PopFont();
    }
}

void HudElements::vulkan_driver(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_vulkan_driver] && !HUDElements.sw_stats->driverName.empty()){
        ImGui::TableNextRow();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.sw_stats->colors.engine,
            "%s", HUDElements.sw_stats->driverName.c_str());
        ImGui::PopFont();
    }
}

void HudElements::arch(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_arch]){
        ImGui::TableNextRow();
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.sw_stats->colors.engine, "%s", "" MANGOHUD_ARCH);
        ImGui::PopFont();
    }
}

void HudElements::wine(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_wine]){
        ImGui::TableNextRow();
        if (!wineVersion.empty()){
            ImGui::PushFont(HUDElements.sw_stats->font1);
            ImGui::TextColored(HUDElements.sw_stats->colors.wine, "%s", wineVersion.c_str());
            ImGui::PopFont();
        }
    }
}

void HudElements::frame_timing(){
    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_frame_timing]){
        ImGui::TableNextRow();
        ImGui::Dummy(ImVec2(0.0f, real_font_size.y));
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::TextColored(HUDElements.sw_stats->colors.engine, "%s", "Frametime");
        ImGui::PopFont();
        ImGui::TableNextRow();
        char hash[40];
        snprintf(hash, sizeof(hash), "##%s", overlay_param_names[OVERLAY_PARAM_ENABLED_frame_timing]);
        HUDElements.sw_stats->stat_selector = OVERLAY_PLOTS_frame_timing;
        HUDElements.sw_stats->time_dividor = 1000.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        double min_time = 0.0f;
        double max_time = 50.0f;
        if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_histogram]){
        ImGui::PlotHistogram(hash, get_time_stat, HUDElements.sw_stats,
                                ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                                NULL, min_time, max_time,
                                ImVec2(ImGui::GetContentRegionAvailWidth() * 2.5, 50));
        } else {
        ImGui::PlotLines(hash, get_time_stat, HUDElements.sw_stats,
                    ARRAY_SIZE(HUDElements.sw_stats->frames_stats), 0,
                    NULL, min_time, max_time,
                    ImVec2(ImGui::GetContentRegionAvailWidth() * 2.5, 50));
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(0,1.0f);
        ImGui::PushFont(HUDElements.sw_stats->font1);
        ImGui::Text("%.1f ms", 1000 / HUDElements.sw_stats->fps); //frame_timing / 1000.f);
        ImGui::PopFont();
    }
}

void HudElements::media_player(){
#ifdef HAVE_DBUS
    ImGui::TableNextRow();
    uint32_t f_idx = (HUDElements.sw_stats->n_frames - 1) % ARRAY_SIZE(HUDElements.sw_stats->frames_stats);
    uint64_t frame_timing = HUDElements.sw_stats->frames_stats[f_idx].stats[OVERLAY_PLOTS_frame_timing];
    ImFont scaled_font = *HUDElements.sw_stats->font_text;
    scaled_font.Scale = HUDElements.params->font_scale_media_player;
    ImGui::PushFont(&scaled_font);
    {
        std::lock_guard<std::mutex> lck(main_metadata.mtx);
        render_mpris_metadata(*HUDElements.params, main_metadata, frame_timing, true);
    }
    ImGui::PopFont();
#endif
}

void HudElements::sort_elements(std::string string){
    if (string == "version")
        ordered_functions.push_back(version);
    if (string == "time")
        ordered_functions.push_back(time);
    if (string == "gpu_stats")
        ordered_functions.push_back(gpu_stats);
    if (string == "cpu_stats")
        ordered_functions.push_back(cpu_stats);
    if (string == "core_load")
        ordered_functions.push_back(core_load);
    if (string == "io_stats")
        ordered_functions.push_back(io_stats);
    if (string == "vram")
        ordered_functions.push_back(vram);
    if (string == "ram")
        ordered_functions.push_back(ram);
    if (string == "fps")
        ordered_functions.push_back(fps);
    if (string == "engine_version")
        ordered_functions.push_back(engine_version);
    if (string == "gpu_name")
        ordered_functions.push_back(gpu_name);
    if (string == "vulkan_driver")
        ordered_functions.push_back(vulkan_driver);
    if (string == "arch")
        ordered_functions.push_back(arch);
    if (string == "wine")
        ordered_functions.push_back(wine);
    if (string == "frame_timing")
        ordered_functions.push_back(frame_timing);
    if (string == "media_player")
        ordered_functions.push_back(media_player);
}

HudElements HUDElements;
