#include "intel.h"
std::unique_ptr<Intel> intel;

void Intel::intel_gpu_thread(){
    init = true;
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

                if  (j.contains("engines"))
                    if (j["engines"].contains("Render/3D"))
                        if (j["engines"]["Render/3D"].contains("busy"))
                            gpu_info_intel.load = j["engines"]["Render/3D"]["busy"].get<int>();

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
        if (stop)
            break;
    }

    int exitcode = pclose(intel_gpu_top) / 256;
    if (exitcode > 0){
        if (exitcode == 127)
        SPDLOG_INFO("Failed to open '{}'", "intel_gpu_top");

        if (exitcode == 1)
        SPDLOG_INFO("Missing permissions for '{}'", "intel_gpu_top");

    }
}

uint64_t Intel::get_gpu_time() {
    rewind(fdinfo);
    fflush(fdinfo);
    char line[256];
    uint64_t val;
    while (fgets(line, sizeof(line), fdinfo)){
        if(strstr(line, "drm-engine-render"))
            sscanf(line, "drm-engine-render: %" SCNu64 " ns", &val);
    }

    return val;
}

FILE* Intel::find_fd() {
    DIR* dir = opendir("/proc/self/fdinfo");
    if (!dir) {
        perror("Failed to open directory");
        return NULL;
    }

    static uint64_t val;
    static bool found_driver;

    for (const auto& entry : fs::directory_iterator("/proc/self/fdinfo")){
        FILE* file = fopen(entry.path().string().c_str(), "r");
        if (file) {
            char line[256];
            while (fgets(line, sizeof(line), file)) {
                if (strstr(line, "i915") != NULL)
                    found_driver = true;

                if (found_driver){
                    if(strstr(line, "drm-engine-render")){
                        sscanf(line, "drm-engine-render: %" SCNu64 " ns", &val);
                            return file;
                    }
                }
            }
        }
        fclose(file);
    }

    return NULL;  // Return NULL if no matching file is found
}

void Intel::get_fdinfo(){
    static uint64_t previous_gpu_time, previous_time, now, gpu_time_now;
    gpu_time_now = get_gpu_time();
    now = os_time_get_nano();

    if (previous_time && previous_gpu_time && gpu_time_now > previous_gpu_time){
        float time_since_last = now - previous_time;
        float gpu_since_last = gpu_time_now - previous_gpu_time;
        auto result = int((gpu_since_last / time_since_last) * 100);
        if (result > 100)
            result = 100;

        gpu_info_intel.load = result;
        previous_gpu_time = gpu_time_now;
        previous_time = now;
    } else {
        previous_gpu_time = gpu_time_now;
        previous_time = now;
    }
}
