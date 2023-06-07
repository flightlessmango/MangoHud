#include <thread>
#include "overlay.h"
#include "gpu.h"
#include "spdlog/spdlog.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

static bool init_intel = false;
struct gpuInfo gpu_info_intel {};

static void intelGpuThread(bool runtime){
    init_intel = true;
    static char stdout_buffer[1024];
    static FILE* intel_gpu_top;
    if (runtime)
        intel_gpu_top = popen("steam-runtime-launch-client --alongside-steam --host -- intel_gpu_top -J -s 500", "r");
    else
        intel_gpu_top = popen("intel_gpu_top -J -s 500", "r");

    int num_line = 0;
    std::string buf;
    int num_iterations = 0;
    while (fgets(stdout_buffer, sizeof(stdout_buffer), intel_gpu_top)) {
        if (num_line > 0)
            buf += stdout_buffer;

        num_line++;
        if (strlen(stdout_buffer) < 4 && !strchr(stdout_buffer, '{') && !strchr(stdout_buffer, ',')) {
            if (buf[0] != '{')
                buf = "{\n" + buf;

            if (num_iterations > 0){
                buf += "\n}";
                json j = json::parse(buf);
                if  (j.contains("engines"))
                    if (j["engines"].contains("Render/3D/0"))
                        if (j["engines"]["Render/3D/0"].contains("busy"))
                            gpu_info_intel.load = j["engines"]["Render/3D/0"]["busy"].get<int>();

                if (j.contains("frequency"))
                    if (j["frequency"].contains("actual"))
                        gpu_info_intel.CoreClock = j["frequency"]["actual"].get<int>();
                if (j.contains("power")){
                    if (j["power"].contains("GPU"))
                        gpu_info_intel.powerUsage = j["power"]["GPU"].get<float>();
                    if (j["power"].contains("Package"))
                        gpu_info_intel.apu_cpu_power = j["power"]["Package"].get<float>();
                }

            }
            buf = "";
            num_line = 0;
        }
        num_iterations++;
    }

    int exitcode = pclose(intel_gpu_top) / 256;
    if (exitcode > 0){
        if (exitcode == 127)
        SPDLOG_INFO("Failed to open '{}'", "intel_gpu_top");

        if (exitcode == 1)
        SPDLOG_INFO("Missing permissions for '{}'", "intel_gpu_top");

        SPDLOG_INFO("Disabling gpu_stats");
        _params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
    }
}

void getIntelGpuInfo(){
    if (!init_intel){
        static bool runtime = false;
        int pressure_vessel = system("steam-runtime-launch-client --alongside-steam --host >/dev/null 2>&1");
        pressure_vessel /= 256;
        switch (pressure_vessel){
            case 127:
                SPDLOG_DEBUG("We're not inside the steam runtime container, run mango_intel_stats normally.");
                break;

            case 125:
                SPDLOG_DEBUG("We're inside the steam runtime container, but --alongside-steam is not implemented yet.");
                SPDLOG_DEBUG("Disabling gpu stats");
                _params->enabled[OVERLAY_PARAM_ENABLED_gpu_stats] = false;
                break;

            case 0:
                SPDLOG_DEBUG("We're inside the steam runtime container, run mango_intel_stats through steam-runtime-launch-client");
                runtime = true;
                break;
        }

        std::thread(intelGpuThread, runtime).detach();
    }

    gpu_info = gpu_info_intel;
}
