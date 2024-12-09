#pragma once
#include <cstdint>
#include <filesystem.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <thread>

#ifdef TEST_ONLY
#include <../src/mesa/util/os_time.h>
#else
#include <mesa/util/os_time.h>
#endif

#include "gpu_metrics_util.h"
#include <atomic>
#include <spdlog/spdlog.h>
#include <map>
#include <set>
#include <regex>

struct hwmon_sensor {
    std::regex rx;
    std::ifstream stream;
    std::string filename;
    unsigned char id = 0;
    uint64_t val = 0;
};

enum GPU_throttle_status : int {
    POWER = 0b0001,
    CURRENT = 0b0010,
    TEMP = 0b0100,
    OTHER = 0b1000,
};

class GPU_fdinfo {
private:
    bool init = false;
    pid_t pid = getpid();

    const std::string module;
    const std::string pci_dev;

    std::thread thread;
    std::condition_variable cond_var;

    std::atomic<bool> stop_thread { false };
    std::atomic<bool> paused { false };

    struct gpu_metrics metrics;
    mutable std::mutex metrics_mutex;

    std::vector<std::ifstream> fdinfo;

    std::map<std::string, hwmon_sensor> hwmon_sensors;

    std::string drm_engine_type = "EMPTY";
    std::string drm_memory_type = "EMPTY";

    std::vector<std::map<std::string, std::string>> fdinfo_data;
    void gather_fdinfo_data();

    void main_thread();

    void find_fd();
    void open_fdinfo_fd(std::string path);

    int get_gpu_load();
    uint64_t get_gpu_time();
    uint64_t previous_gpu_time, previous_time = 0;

    std::vector<uint64_t> xe_fdinfo_last_cycles;
    std::map<std::string, std::pair<uint64_t, uint64_t>> prev_xe_cycles;
    int get_xe_load();

    float get_memory_used();

    void find_hwmon();
    void get_current_hwmon_readings();

    float get_power_usage();
    float last_power = 0;

    std::ifstream gpu_clock_stream;
    void find_i915_gt_dir();
    void find_xe_gt_dir();
    int get_gpu_clock();

    std::ifstream throttle_status_stream;
    std::vector<std::ifstream> throttle_power_streams;
    std::vector<std::ifstream> throttle_current_streams;
    std::vector<std::ifstream> throttle_temp_streams;
    bool check_throttle_reasons(std::vector<std::ifstream> &throttle_reason_streams);
    int get_throttling_status();

    const std::vector<std::string> intel_throttle_power = {"reason_pl1", "reason_pl2"};
    const std::vector<std::string> intel_throttle_current = {"reason_pl4", "reason_vr_tdc"};
    const std::vector<std::string> intel_throttle_temp = {
        "reason_prochot", "reason_ratl", "reason_thermal", "reason_vr_thermalert"};
    void load_xe_i915_throttle_reasons(
        std::string throttle_folder,
        std::vector<std::string> throttle_reasons,
        std::vector<std::ifstream> &throttle_reason_streams);

public:
    GPU_fdinfo(const std::string module, const std::string pci_dev)
        : module(module)
        , pci_dev(pci_dev)
    {
        SPDLOG_DEBUG("GPU driver is \"{}\"", module);

        find_fd();
        gather_fdinfo_data();

        if (module == "i915") {
            drm_engine_type = "drm-engine-render";
            drm_memory_type = "drm-total-local0";
        } else if (module == "xe") {
            drm_engine_type = "drm-total-cycles-rcs";
            drm_memory_type = "drm-resident-vram0";

            if (
                fdinfo_data.size() > 0 &&
                fdinfo_data[0].find(drm_memory_type) == fdinfo_data[0].end()
            ) {
                SPDLOG_DEBUG(
                    "\"{}\" is not found, you probably have an integrated GPU. "
                    "Using \"drm-resident-gtt\".", drm_memory_type
                );
                drm_memory_type = "drm-resident-gtt";
            }
        } else if (module == "amdgpu") {
            drm_engine_type = "drm-engine-gfx";
            drm_memory_type = "drm-memory-vram";
        } else if (module == "msm") {
            // msm driver does not report vram usage
            drm_engine_type = "drm-engine-gpu";
        }

        SPDLOG_DEBUG(
            "drm_engine_type = {}, drm_memory_type = {}",
            drm_engine_type, drm_memory_type
        );

        hwmon_sensors["voltage"]   = { .rx = std::regex("in(\\d+)_input") };
        hwmon_sensors["fan_speed"] = { .rx = std::regex("fan(\\d+)_input") };
        hwmon_sensors["temp"]      = { .rx = std::regex("temp(\\d+)_input") };
        hwmon_sensors["power"]     = { .rx = std::regex("power(\\d+)_input") };
        hwmon_sensors["energy"]    = { .rx = std::regex("energy(\\d+)_input") };

        find_hwmon();

        if (module == "i915")
            find_i915_gt_dir();
        else if (module == "xe")
            find_xe_gt_dir();

        std::thread thread(&GPU_fdinfo::main_thread, this);
        thread.detach();
    }

    gpu_metrics copy_metrics() const
    {
        return metrics;
    };

    void pause()
    {
        paused = true;
        cond_var.notify_one();
    }

    void resume()
    {
        paused = false;
        cond_var.notify_one();
    }
};
