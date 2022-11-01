#include <thread>
#include "overlay.h"
#include "gpu.h"
#include "spdlog/spdlog.h"

static bool init_intel = false;
struct gpuInfo gpu_info_intel {};
void intelGpuThread(){
    init_intel = true;
    static char stdout_buffer[1024];
    FILE* intel_gpu_top = popen("intel_gpu_top -l -s 500", "r");
    while (fgets(stdout_buffer, sizeof(stdout_buffer), intel_gpu_top)) {
        if (strstr(stdout_buffer, "Freq") == NULL &&
            strstr(stdout_buffer, "req") == NULL) {
        char * pch;
        pch = strtok(stdout_buffer, " ");
        int i = 0;
        while (pch != NULL){
            switch (i){
            case 0:
                gpu_info_intel.CoreClock = atoi(pch);
                break;
            case 4:
                gpu_info_intel.powerUsage = atof(pch);
                break;
            case 5:
                gpu_info_intel.apu_cpu_power = atof(pch);
                break;
            case 8:
                gpu_info_intel.load = atoi(pch);
                break;
            }
            pch = strtok(NULL, " ");
            i++;
        }
        }
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
    if (!init_intel)
        std::thread(intelGpuThread).detach();

    gpu_info = gpu_info_intel;
}