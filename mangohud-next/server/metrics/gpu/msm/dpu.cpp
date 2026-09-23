#include "dpu.hpp"
#include <cmath>
#include <fstream>
#include <filesystem>
#include "gpu_metrics_util.h"
#include "string_utils.h"

#ifdef HAVE_TRACEFS_GPU_MEM
#include <cerrno>
#include <cstring>
extern "C" {
#include <tracefs.h>
}
#endif

namespace fs = std::filesystem;

#ifdef HAVE_TRACEFS_GPU_MEM
class MSM_GPUMemTrace {
public:
    MSM_GPUMemTrace()
    {
        if (tracefs_instance_exists(instance_name))
            instance = tracefs_instance_alloc(nullptr, instance_name);
        else
            instance = tracefs_instance_create(instance_name);

        if (!instance) {
            SPDLOG_DEBUG("tracefs: failed to create instance '{}': {}", instance_name, std::strerror(errno));
            return;
        }

        char* dir = tracefs_instance_get_dir(instance);
        if (!dir) {
            SPDLOG_DEBUG("tracefs: failed to get instance dir for '{}': {}", instance_name, std::strerror(errno));
            return;
        }

        tep = tracefs_local_events(dir);
        tracefs_put_tracing_file(dir);
        if (!tep) {
            SPDLOG_DEBUG("tracefs: failed to read local events for '{}': {}", instance_name, std::strerror(errno));
            return;
        }

        tracefs_event_disable(instance, nullptr, nullptr);
        errno = 0;
        int ret = tracefs_event_enable(instance, "gpu_mem", "gpu_mem_total");
        if (ret < 0) {
            SPDLOG_DEBUG("tracefs: failed to enable gpu_mem/gpu_mem_total: {}", errno ? std::strerror(errno) : "event not found");
            return;
        }

        enabled = true;
        SPDLOG_DEBUG("tracefs: enabled gpu_mem/gpu_mem_total");
    }

    ~MSM_GPUMemTrace()
    {
        if (instance)
            tracefs_event_disable(instance, "gpu_mem", "gpu_mem_total");

        if (tep)
            tep_free(tep);

        if (instance) {
            if (tracefs_instance_is_new(instance))
                tracefs_instance_destroy(instance);
            tracefs_instance_free(instance);
        }
    }

    float get_total_used_gib()
    {
        if (!enabled)
            return 0.0f;

        tracefs_iterate_raw_events(tep, instance, nullptr, 0, records_walk, this);
        return static_cast<float>(bytes) / (1024.0f * 1024.0f * 1024.0f);
    }

private:
    static constexpr const char* instance_name = "mangohud";
    struct tracefs_instance* instance = nullptr;
    struct tep_handle* tep = nullptr;
    unsigned long long bytes = 0;
    bool enabled = false;

    static int records_walk(struct tep_event*, struct tep_record* record, int, void* context)
    {
        auto* self = static_cast<MSM_GPUMemTrace*>(context);
        struct tep_event* event = tep_find_event_by_record(self->tep, record);
        if (!event)
            return 0;

        for (struct tep_format_field* field = event->format.fields; field; field = field->next) {
            unsigned long long value = tep_read_number(
                self->tep,
                static_cast<char*>(record->data) + field->offset,
                field->size
            );

            // pid 0 is the aggregate device usage record, not a process.
            if (!std::strcmp(field->name, "pid") && value != 0)
                return 0;

            if (!std::strcmp(field->name, "size"))
                self->bytes = value;
        }

        return 0;
    }
};
#else
class MSM_GPUMemTrace {
public:
    float get_total_used_gib() { return 0.0f; }
};
#endif

MSM_DPU::MSM_DPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) : GPU(drm_node, pci_dev, vendor_id, device_id, "gpu-msm-dpu"), FDInfo(drm_node) {
    steam_frame = drm_node_is_steam_frame(drm_node);
    gpu_mem_trace = std::make_unique<MSM_GPUMemTrace>();
    hwmon.base_dir = hwmon.find_hwmon_dir_by_name("gpu");
    hwmon.setup(sensors, drm_node);
    junction_temp_file = open_thermal_zone("gpuss-0-thermal");
    memory_temp_file = open_thermal_zone("ddr-thermal");
    core_clock_file = open_core_clock();
    load_file = open_load();
}

MSM_DPU::~MSM_DPU() = default;

void MSM_DPU::pre_poll_overrides() {
    hwmon.poll_sensors();
    fdinfo.poll_all();
}

int MSM_DPU::get_temperature() {
    return static_cast<int>(::lroundf(hwmon.get_sensor_value("temp") / 1000.0f));
}

int MSM_DPU::get_load() {
    if (!load_file.is_open())
        return -1;

    load_file.clear();
    load_file.seekg(0, std::ios::beg);

    float load = 0.0f;
    if (!(load_file >> load))
        return -1;

    return static_cast<int>(::lroundf(load));
}

float MSM_DPU::get_vram_used() {
    return gpu_mem_trace ? gpu_mem_trace->get_total_used_gib() : 0.0f;
}

std::ifstream MSM_DPU::open_thermal_zone(const std::string& type) {
    std::ifstream file;
    const fs::path sysfs_thermal = "/sys/class/thermal";

    if (!fs::exists(sysfs_thermal))
        return file;

    for (auto& entry : fs::directory_iterator(sysfs_thermal)) {
        if (!entry.path().filename().string().starts_with("thermal_zone"))
            continue;

        std::ifstream type_file(entry.path() / "type");
        std::string zone_type;
        std::getline(type_file, zone_type);
        if (zone_type != type)
            continue;

        file.open(entry.path() / "temp");
        if (file.is_open())
            SPDLOG_INFO("thermal: using {} input: {}", type, (entry.path() / "temp").string());
        return file;
    }

    return file;
}

std::ifstream MSM_DPU::open_core_clock() {
    std::ifstream file;
    const fs::path sysfs_devfreq = "/sys/class/devfreq";

    if (!fs::exists(sysfs_devfreq))
        return file;

    for (auto& entry : fs::directory_iterator(sysfs_devfreq)) {
        std::string name = entry.path().filename().string();
        if (!name.ends_with(".gpu"))
            continue;

        fs::path cur_freq = entry.path() / "cur_freq";
        file.open(cur_freq);
        if (file.is_open())
            SPDLOG_INFO("devfreq: using gpu clock input: {}", cur_freq.string());
        return file;
    }

    return file;
}

std::ifstream MSM_DPU::open_load() {
    std::ifstream file;
    fs::path drm_dir = fs::path("/sys/class/drm") / drm_node / "device/drm";

    if (!fs::exists(drm_dir))
        return file;

    for (auto& entry : fs::directory_iterator(drm_dir)) {
        std::string name = entry.path().filename().string();
        if (!name.starts_with("card"))
            continue;

        fs::path perf_now = fs::path("/sys/kernel/debug/dri") / name.substr(4) / "perf_now";
        file.open(perf_now);
        if (file.is_open())
            SPDLOG_INFO("debugfs: using msm load input: {}", perf_now.string());
        return file;
    }

    return file;
}

int MSM_DPU::read_thermal_zone(std::ifstream& file) {
    if (!file.is_open())
        return 0;

    file.clear();
    file.seekg(0, std::ios::beg);

    int temp = 0;
    if (!(file >> temp))
        return 0;

    return static_cast<int>(::lroundf(temp / 1000.0f));
}

int MSM_DPU::get_junction_temperature() {
    return read_thermal_zone(junction_temp_file);
}

int MSM_DPU::get_memory_temp() {
    return read_thermal_zone(memory_temp_file);
}

float MSM_DPU::get_memory_total() {
    return steam_frame ? 16.0f : 0.0f;
}

int MSM_DPU::get_memory_clock() {
    return steam_frame ? 4200 : 0;
}

int MSM_DPU::get_core_clock() {
    if (!core_clock_file.is_open())
        return 0;

    core_clock_file.clear();
    core_clock_file.seekg(0, std::ios::beg);

    int clock = 0;
    if (!(core_clock_file >> clock))
        return 0;

    return clock / 1'000'000;
}

float MSM_DPU::get_power_usage() {
    std::ifstream file("/run/power-monitor/power/gfx");
    if (file.fail())
        return 0.0f;

    std::string value;
    std::getline(file, value);
    if (value.empty())
        return 0.0f;

    float power = 0.0f;
    if (try_stof(power, value))
        return power;

    return 0.0f;
}

int MSM_DPU::get_process_load(pid_t pid) {
    uint64_t* previous_gpu_time = &previous_gpu_times[pid];

    uint64_t gpu_time_now = fdinfo.get_gpu_time(pid, "drm-engine-gpu");

    if (!*previous_gpu_time) {
        *previous_gpu_time = gpu_time_now;
        return 0;
    }

    float delta_gpu_time = gpu_time_now - *previous_gpu_time;
    float result = delta_gpu_time / delta_time_ns.count() * 100;

    if (result > 100.f)
        result = 100.f;

    *previous_gpu_time = gpu_time_now;

    return static_cast<int>(::lroundf(result));
}

float MSM_DPU::get_process_vram_used(pid_t pid) {
    return fdinfo.get_memory_used(pid, "drm-resident-memory");
}
